package main

import (
	"bytes"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"math"
	"net/http"
	"net/url"
	"os"
	"os/exec"
	"path"
	"runtime"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unsafe"

	"github.com/datacratic/goredis/redis"
)

type Database struct {
	Client redis.Client

	store    *redis.DB
	inflight int64
	get      string
}

type Router struct {
	Host string

	white *Database
	black *Database
	mutex sync.RWMutex
	reads unsafe.Pointer
	write unsafe.Pointer
}

type Metrics struct {
	ViewableRate      float64 `json:"vr"`
	MeasuredRate      float64 `json:"mr"`
	CompletedViewRate float64 `json:"cvr"`
	ClickThroughRate  float64 `json:"ctr"`
	VCVRate           float64 `json:"vcvr"`
}

func main() {
	address := flag.String("address", ":9200", "address of the API server")
	black := flag.String("black", "", "address of a REDIS cluster")
	white := flag.String("white", "", "address of a REDIS cluster")
	color := flag.String("color", "black", "name of the color handling reads")
	lookup := flag.String("lookup", "full", "Format of lookup to use (full or partial)")
	cpu := flag.Int("cpu", 1, "number of CPU to use")

	flag.Parse()
	output, err := exec.Command("hostname", "-f").Output()
	if err != nil {
		log.Fatal(err)
	}

	router := &Router{
		Host: "http://" + strings.TrimSpace(string(output)) + *address,
	}

	if *black == "" {
		db, err := redis.NewTestDB()
		if err != nil {
			log.Fatal(err)
		}

		router.black = &Database{Client: redis.Client{Address: []string{db.URL()}}, store: db}
	} else {
		router.black = &Database{Client: redis.Client{Address: strings.Split(*black, ",")}}
	}

	if *white == "" {
		db, err := redis.NewTestDB()
		if err != nil {
			log.Fatal(err)

		}

		router.white = &Database{Client: redis.Client{Address: []string{db.URL()}}, store: db}
	} else {
		router.white = &Database{Client: redis.Client{Address: strings.Split(*white, ",")}}
	}

	switch *color {
	case "black":
		router.reads = unsafe.Pointer(router.black)
		router.write = unsafe.Pointer(router.white)
		log.Println(router.black.Client.Address, "(reads)")
		log.Println(router.white.Client.Address)
	case "white":
		router.reads = unsafe.Pointer(router.white)
		router.write = unsafe.Pointer(router.black)
		log.Println(router.white.Client.Address, "(reads)")
		log.Println(router.black.Client.Address)
	default:
		log.Fatal("color must be either 'black' or 'white'")
	}

	fullGet := `
		-- KEYS is { Host }
		-- ARGV is { Path, Width, Exchange, Publisher, Position }

		local h = "{"..KEYS[1].."}"
		local u = h..ARGV[1]
		local w = ":"..ARGV[2]
		local p = ":"..ARGV[3]..":"..ARGV[4]
		local q = ":"..ARGV[5]

		if w ~= ":" and p ~= "::" then
			-- url+width+publisher+position
			if q ~= ":" then
				local x = redis.pcall("GET", u..w..p..q)
				if x then
				    return {x, "Hit1PageWidthPublisherPosition"}
				end
			end

			-- url+width+publisher
			local x = redis.pcall("GET", u..w..p..":")
			if x then
			    return {x, "Hit2PageWidthPublisher"}
			end
		end
		-- url+width
		if w ~= ":" then
			local x = redis.pcall("GET", u..w..":::")
			if x then
			    return {x, "Hit3PageWidth"}
			end
		end
		-- url+publisher
		if p ~= "::" then
			local x = redis.pcall("GET", u..":"..p..":")
			if x then
			    return {x, "Hit4PagePublisher"}
			end
		end
		-- url
		local x = redis.call("GET", u.."::::")
		if x then
		    return {x, "Hit5Page"}
		end
		-- domain+width+publisher
		if w ~= ":" and p ~= "::" then
			local x = redis.pcall("GET", h..w..p..":")
			if x then
			    return {x, "Hit6DomainWidthPublisher"}
			end
		end
		-- domain+publisher
		if p ~= "::" then
			local x = redis.pcall("GET", h..":"..p..":")
			if x then
			    return {x, "Hit7DomainPublisher"}
			end
		end
		-- domain+width
		if w ~= ":" then
			local x = redis.call("GET", h..w..":::")
			if x then
			    return {x, "Hit8DomainWidth"}
			end
		end
		-- domain
		x = redis.pcall("GET", h.."::::")
		if x then
		    return {x, "Hit9Domain"}
		end
		return
	`

	partialGet := `
		-- KEYS is { Host }
		-- ARGV is { Path, Width, Exchange, Publisher, Position }

		local h = "{"..KEYS[1].."}"
		local u = h..ARGV[1]
		local w = ":"..ARGV[2]
		local p = ":"..ARGV[3]..":"..ARGV[4]
		local q = ":"..ARGV[5]

		if w ~= ":" and p ~= "::" then
			-- url+width+publisher+position
			if q ~= ":" then
				local x = redis.pcall("GET", u..w..p..q)
				if x then
				    return {x, "Hit1PageWidthPublisherPosition"}
				end
			end

			-- url+width+publisher
			local x = redis.pcall("GET", u..w..p..":")
			if x then
			    return {x, "Hit2PageWidthPublisher"}
			end
		end
		-- url+width
		if w ~= ":" then
			local x = redis.pcall("GET", u..w..":::")
			if x then
			    return {x, "Hit3PageWidth"}
			end
		end
		-- url+publisher
		if p ~= "::" then
			local x = redis.pcall("GET", u..":"..p..":")
			if x then
			    return {x, "Hit4PagePublisher"}
			end
		end
		-- url
		local x = redis.call("GET", u.."::::")
		if x then
		    return {x, "Hit5Page"}
		end
		return
	`

	get := fullGet
	if lookup != nil && *lookup == "partial" {
		get = partialGet
	}

	router.white.get, err = router.white.Client.LuaScript(get)
	if err != nil {
		log.Panic(err)
	}

	router.black.get, err = router.black.Client.LuaScript(get)
	if err != nil {
		log.Panic(err)
	}

	http.HandleFunc("/record", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			http.Error(w, "405 method not allowed", http.StatusMethodNotAllowed)
			return
		}

		body := r.Body.(io.Reader)

		file, err := ioutil.TempFile(path.Join(path.Dir(os.Args[0])), "csv-")
		if err != nil {
			log.Println("can't create temporary file")
		} else {
			body = io.TeeReader(r.Body, file)
			defer file.Close()
		}

		router.mutex.RLock()
		defer router.mutex.RUnlock()
		defer r.Body.Close()

		p := router.write
		d := (*Database)(p)

		line := 0

		// named index of fields in 'cols'
		const (
			source    = 0
			width     = 1
			exchange  = 2
			publisher = 3
			position  = 4
			metrics   = 5 // placeholder for list of metrics
			vr        = 5
			mr        = 6
			cvr       = 7
			ctr       = 8
			vcvr      = 9
			total     = 10
		)

		cols := make([]int, total)

		reader := csv.NewReader(body)
		for {
			list, err := reader.Read()
			if err == io.EOF {
				break
			}

			if err != nil {
				http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
				return
			}

			line++
			if line == 1 {
				tags := make(map[string]int)
				for i, title := range list {
					tags[title] = i
				}

				// must match the named index of fields above
				names := []string{
					"Embedding Page URL",
					"Player width",
					"rtbExch",
					"rtb_Publisher_Site",
					"rtbPos",
					"IAB Viewable Rate",
					"IAB Measured Rate",
					"100% Completed View Rate",
					"Click through Rate",
					"VCV Rate",
				}

				for i, name := range names {
					k, ok := tags[name]
					if !ok && i == source {
						k, ok = tags["Media"]
					}

					if !ok {
						k = -1
					}

					cols[i] = k
				}

				if cols[source] == -1 {
					http.Error(w, "400 bad request\nMissing domain or page URL\n", http.StatusBadRequest)
					return
				}

				continue
			}

			values := make([]float64, 5)
			for i := range values {
				k := cols[metrics+i]
				if k == -1 {
					continue
				}

				x, err := strconv.ParseFloat(list[k], 64)
				if err != nil {
					http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
					return
				}

				values[i] = math.Floor(x * 100)
			}

			m := Metrics{
				ViewableRate:      values[0],
				MeasuredRate:      values[1],
				CompletedViewRate: values[2],
				ClickThroughRate:  values[3],
				VCVRate:           values[4],
			}

			payload, err := json.Marshal(&m)
			if err != nil {
				http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
				return
			}

			text := list[cols[source]]

			if !strings.HasPrefix(text, "http://") {
				text = strings.TrimPrefix(text, "http://")
			}

			if !strings.HasPrefix(text, "https://") {
				text = strings.TrimPrefix(text, "https://")
			}

			if !strings.HasPrefix(text, "apps://") {
				text = strings.TrimPrefix(text, "apps://")
			}

			text = strings.TrimSuffix(text, "/")

			u, err := url.Parse("http://" + text)
			if err != nil {
				u = &url.URL{Host: text}
			}

			get := func(i int) string {
				if cols[i] == -1 {
					return ""
				}

				return list[cols[i]]
			}

			if u.Host == "" {
				continue
			}

			ext := []string{
				u.Path,
				get(width),
				get(exchange),
				get(publisher),
				get(position),
			}

			key := fmt.Sprintf("{%s}%s", u.Host, strings.Join(ext, ":"))

			_, err = d.Client.Do("SET", key, payload)
			if err != nil {
				http.Error(w, "503 service not available\n"+err.Error(), http.StatusServiceUnavailable)
				return
			}
		}

		output := fmt.Sprintf("%d lines imported\n", line)

		w.Header().Set("Content-Type", "text/plain")
		w.Header().Set("Content-Length", fmt.Sprintf("%d", len(output)))
		io.WriteString(w, output)
	})

	http.HandleFunc("/switch", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			http.Error(w, "405 method not allowed", http.StatusMethodNotAllowed)
			return
		}

		router.mutex.Lock()
		defer router.mutex.Unlock()
		defer r.Body.Close()

		p := router.reads
		atomic.StorePointer(&router.reads, router.write)

		// wait for in-flight requests
		d := (*Database)(p)
		for atomic.LoadInt64(&d.inflight) != 0 {
			time.Sleep(time.Second)
		}

		_, err := d.Client.Do("FLUSHALL")
		if err != nil {
			http.Error(w, "503 service not available\n"+err.Error(), http.StatusServiceUnavailable)
		}

		log.Println("now writing to", d.Client.Address)

		router.write = p
		w.WriteHeader(http.StatusOK)
	})

	http.HandleFunc("/viewability", func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "POST" {
			http.Error(w, "405 method not allowed", http.StatusMethodNotAllowed)
			return
		}

		defer r.Body.Close()

		q := struct {
			Exchange  string `json:"exchange"`
			Publisher string `json:"publisher"`
			PageURL   string `json:"url"`
			Width     *int   `json:"w"`
			Position  string `json:"position"`
		}{}

		err := json.NewDecoder(r.Body).Decode(&q)
		if err != nil {
			log.Println("ERROR", "BAD", err.Error())
			http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
			return
		}

		text := q.PageURL

		if !strings.HasPrefix(text, "http://") {
			text = strings.TrimPrefix(text, "http://")
		}

		if !strings.HasPrefix(text, "https://") {
			text = strings.TrimPrefix(text, "https://")
		}

		if !strings.HasPrefix(text, "apps://") {
			text = strings.TrimPrefix(text, "apps://")
		}

		text = strings.TrimSuffix(text, "/")

		u, err := url.Parse("http://" + text)
		if err != nil {
			u = &url.URL{Host: text}
		}

		x := ""
		if q.Width != nil {
			x = fmt.Sprintf("%d", *q.Width)
		}

		p := atomic.LoadPointer(&router.reads)
		d := (*Database)(p)
		atomic.AddInt64(&d.inflight, 1)

		result, err := d.Client.Do("EVALSHA", d.get, 1, u.Host, u.Path, x, q.Exchange, q.Publisher, q.Position)
		atomic.AddInt64(&d.inflight, -1)
		if err != nil {
			log.Println("ERROR", "REDIS", err.Error())
			http.Error(w, "503 service not available\n"+err.Error(), http.StatusServiceUnavailable)
			return
		}

		if result == nil {
			w.WriteHeader(http.StatusNoContent)
			return
		}

		items := result.([]interface{})

		body := bytes.Buffer{}

		value := items[0].([]byte)
		stage := items[1].([]byte)

		m := Metrics{}
		if err := json.Unmarshal(value, &m); err != nil {
			log.Println("ERROR", "JSON", err.Error())
			http.Error(w, "503 service not available\n"+err.Error(), http.StatusServiceUnavailable)
		}

		score := fmt.Sprintf("%f", m.ViewableRate)

		body.WriteString(`{"score":`)
		body.WriteString(score)
		body.WriteString(`,"stage":"`)
		body.Write(stage)
		body.WriteString(`","metrics":`)
		body.Write(value)
		body.WriteString(`}`)

		w.Header().Set("Content-Type", "application/json")
		w.Header().Set("Content-Length", fmt.Sprintf("%d", body.Len()))
		w.Write(body.Bytes())
	})

	log.Println("listening on", router.Host)

	if cpu != nil {
		runtime.GOMAXPROCS(*cpu)
		log.Printf("using %d CPU(s)\n", *cpu)
	}

	err = http.ListenAndServe(*address, nil)
	if err != nil {
		log.Fatal(err)
	}
}

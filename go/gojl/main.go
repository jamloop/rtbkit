package main

import (
	"bytes"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"log"
	"math"
	"net/http"
	"net/url"
	"os/exec"
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
    if lookup == "partial"
        get = partialGet

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

		router.mutex.RLock()
		defer router.mutex.RUnlock()
		defer r.Body.Close()

		p := router.write
		d := (*Database)(p)

		line := 0
		cols := make([]int, 6)

		reader := csv.NewReader(r.Body)
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

				names := []string{
					"IAB Viewable Rate",
					"Player width",
					"Embedding Page URL",
					"rtbExch",
					"rtb_Publisher_Site",
					"rtbPos",
				}

				for i, name := range names {
					k, ok := tags[name]
					if !ok && i == 2 {
						k, ok = tags["Media"]
					}

					if !ok {
						k = -1
					}

					cols[i] = k
				}

				if cols[0] == -1 {
					http.Error(w, "400 bad request\nMissing viewability column\n", http.StatusBadRequest)
					return
				}

				if cols[2] == -1 {
					http.Error(w, "400 bad request\nMissing domain or page URL\n", http.StatusBadRequest)
					return
				}

				continue
			}

			x, err := strconv.ParseFloat(list[cols[0]], 64)
			if err != nil {
				http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
				return
			}

			text := list[cols[2]]
			if !strings.HasPrefix(text, "http://") {
				text = "http://" + text
			}

			u, err := url.Parse(text)
			if err != nil {
				http.Error(w, fmt.Sprintf("line %d: %s", line, err), http.StatusBadRequest)
				continue
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

			key := fmt.Sprintf("{%s}%s:%s:%s:%s:%s", u.Host, u.Path, get(1), get(3), get(4), get(5))
			viewability := math.Floor(x * 100)
			_, err = d.Client.Do("SET", key, fmt.Sprintf("%f", viewability))
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
			text = "http://" + text
		}

		u, err := url.Parse(text)
		if err != nil {
			log.Println("ERROR", "URL", err.Error())
			http.Error(w, "400 bad request\n"+err.Error(), http.StatusBadRequest)
			return
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

		score := items[0].([]byte)
		stage := items[1].([]byte)

		body.WriteString(`{"score":`)
		body.Write(score)
		body.WriteString(`,"stage":"`)
		body.Write(stage)
		body.WriteString(`"}`)

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

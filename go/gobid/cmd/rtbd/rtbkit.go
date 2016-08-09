package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io/ioutil"
	"log"
	"math/rand"
	"net/http"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"

	"../../rtb"
)

type Agent struct {
	Account         []string               `json:"account"`
	ID              int                    `json:"externalId"`
	BidProbability  float64                `json:"bidProbability"`
	Creatives       []Creative             `json:"creatives"`
	Configuration   Providers              `json:"providerConfig"`
	DeviceType      *DeviceTypeFilter      `json:"deviceTypeFilter"`
	DMA             *DMAFilter             `json:"dmaFilter"`
	WhiteBlackLists *WhiteBlackListsFilter `json:"whiteBlackList"`
	Augmenters      *Augmenters            `json:"augmentations"`
	Parameters      *BasicBiddingAgent     `json:"ext"`

	content []byte
	state   atomic.Value
}

type Pacing struct {
	requests  int64
	bids      int64
	done      int64
	timestamp time.Time
	qps       float64
	sampling  float64
}

func (a *Agent) post() {
	go func() {
		url := "http://rtb1.useast1b.jamloop:9986/v1/agents/jamloop.useast1b.bidders." + a.Account[0] + "/config"

		dt := time.Second
		for i := 0; i < 3; i++ {
			time.Sleep(dt)

			r, err := http.Post(url, "application/json", bytes.NewReader(a.content))
			if err != nil {
				log.Println(url, err)
				dt += dt
				continue
			}

			if r.StatusCode != http.StatusOK {
				log.Println(url, "ACS error:", r.StatusCode)
				dt += dt
				continue
			}

			break
		}
	}()
}

func (a *Agent) Begin() {
	a.state.Store(&Pacing{})
	a.post()
}

func (a *Agent) Update(ctx context.Context, now time.Time) {
	ctx = trace.Enter(ctx, a.Account[0]+".Pace")
	a.post()

	p := a.state.Load().(*Pacing)

	dt := now.Sub(p.timestamp)
	n := atomic.LoadInt64(&p.requests)
	qps := float64(n) / dt.Seconds()

	m := 0.0
	s := 1.0

	bps := 0.0
	if a.Parameters != nil {
		pace, err := strconv.Atoi(strings.TrimSuffix(a.Parameters.Pace, "USD/1M"))
		if err != nil {
			pace = 0
		}

		price, err := strconv.Atoi(strings.TrimSuffix(a.Parameters.Price, "USD/1M"))
		if err != nil {
			price = 0
		}

		if price != 0 {
			bps = float64(pace) / float64(price) / 60.0 / 2.0
			if bps >= qps {
				s = 1.0
			} else {
				s = bps / qps
			}

			m = bps * dt.Seconds()
		}
	}

	ema := 0.8*p.sampling + 0.2*s

	trace.Set(ctx, "BPS", bps)
	trace.Set(ctx, "QPS", qps)
	//trace.Set(ctx, "SmoothQPS", qps)
	trace.Set(ctx, "Sampling", s)
	trace.Set(ctx, "SamplingEMA", ema)

	a.state.Store(&Pacing{
		bids:      int64(m),
		timestamp: now,
		qps:       qps,
		sampling:  ema,
	})

	trace.Leave(ctx, "Done")
}

func (a *Agent) Bid(ctx context.Context) (price, priority string) {
	p := a.state.Load().(*Pacing)

	atomic.AddInt64(&p.requests, 1)

	if p.sampling < rand.Float64() {
		trace.Count(ctx, "Bidders."+a.Account[0]+".RandomNoBid", 1)
		return
	}

	n := atomic.AddInt64(&p.bids, -1)
	if n < 0 {
		trace.Count(ctx, "Bidders."+a.Account[0]+".NoBid", 1)
		return
	}

	if a.Parameters != nil {
		trace.Count(ctx, "Bidders."+a.Account[0]+".Bid", 1)
		price = a.Parameters.Price
		priority = fmt.Sprintf("%d", a.Parameters.Priority)
	}

	return
}

type Creative struct {
	ID            int       `json:"id"`
	Width         int       `json:"width"`
	Heigth        int       `json:"height"`
	Configuration Providers `json:"providerConfig"`
}

type Providers struct {
	AdapTV *struct {
		Seat     string `json:"seat"`
		AdMarkup string `json:"adm"`
		URL      string `json:"nurl"`
	} `json:"adaptv"`

	BidSwitch *struct {
		Seat      string   `json:"seat"`
		URL       string   `json:"nurl"`
		ImageURL  string   `json:"iurl"`
		AdID      string   `json:"adid"`
		AdDomain  []string `json:"adomain"`
		AdMarkup  string   `json:"adm"`
		Extension struct {
			Advertiser    string `json:"advertiser_name"`
			Agency        string `json:"agency_name"`
			VastURL       string `json:"vast_url"`
			Duration      int    `json:"duration"`
			LandingDomain string `json:"lpdomain"`
			Language      string `json:"language"`
		} `json:"ext"`
	} `json:"bidswitch"`

	BrightRoll *struct {
		Seat        string                 `json:"seat"`
		URL         string                 `json:"nurl"`
		AdDomain    string                 `json:"adomain"`
		Campaign    string                 `json:"campaign_name"`
		LineItem    string                 `json:"line_item_name"`
		Creative    string                 `json:"creative_name"`
		Duration    int                    `json:"creative_duration"`
		Media       map[string]interface{} `json:"media_desc"`
		Version     int                    `json:"api"`
		LandingID   string                 `json:"lid"`
		LandingPage string                 `json:"landingpage_url"`
		Advertiser  string                 `json:"advertiser_name"`
	} `json:"brightroll"`

	Publisher *struct {
		Vast string `json:"vast"`
	} `json:"publisher"`

	SpotX *struct {
		Seat     string   `json:"seat"`
		SeatName string   `json:"seatName"`
		BidID    string   `json:"bidid"`
		AdMarkup string   `json:"adm"`
		AdDomain []string `json:"adomain"`
	} `json:"spotx"`
}

type DeviceTypeFilter struct {
	Include []int `json:"include"`
	Exclude []int `json:"exclude"`
}

type DMAFilter struct {
	Include []string `json:"include"`
	Exclude []string `json:"exclude"`
}

type WhiteBlackListsFilter struct {
	WhiteList string `json:"whiteFile"`
	BlackList string `json:"blackFile"`
}

type Augmenters struct {
	Viewability ViewabilityAugmenter `json:"viewability"`
}

type Augmenter struct {
	Required bool `json:"required"`
	Filters  struct {
		Include []string `json:"include"`
		Exclude []string `json:"exclude"`
	} `json:"filters"`
}

type ViewabilityAugmenter struct {
	Augmenter
	Configuration struct {
		Threshold int    `json:"viewTreshold"`
		Strategy  string `json:"unknownStrategy"`
	} `json:"config"`
}

type BasicBiddingAgent struct {
	Budget   string `json:"budget"`
	Pace     string `json:"pace"`
	Price    string `json:"price"`
	Priority int    `json:"priority"`
}

type Bidders struct {
	Pattern string
	Tracer  trace.Handler
	Name    string

	state atomic.Value
	mu    sync.Mutex
	tick  chan struct{}
}

func (b *Bidders) Start() (err error) {
	b.state.Store(make(map[string]*Agent))

	//log.Println("START")

	update := func() (err error) {
		b.mu.Lock()
		defer b.mu.Unlock()

		log.Println("checking configurations...")

		ctx := trace.Start(trace.SetHandler(context.Background(), b.Tracer), b.Name+".Tick", "")
		last := b.state.Load().(map[string]*Agent)

		agents, err := Import(b.Pattern)
		if err != nil {
			trace.Error(ctx, "Errors.Import", err)
			return
		}

		if len(agents) == 0 {
			log.Println("no files found: ", b.Pattern)
			trace.Leave(ctx, "Errors.NoFiles")
			return
		}

		next := make(map[string]*Agent)
		now := time.Now().UTC()

		for _, agent := range agents {
			id := fmt.Sprintf("%d", agent.ID)
			//log.Println(id, agent.Account[0])
			next[id] = agent
			if a := last[id]; a == nil {
				agent.Begin()
			} else {
				agent.state = a.state
				agent.Update(ctx, now)
			}
		}

		b.state.Store(next)
		trace.Leave(ctx, "Done")
		return
	}

	if err = update(); err != nil {
		log.Println(err)
	}

	b.tick = rtb.PeriodicFunc(rtb.Every(time.Minute), func() {
		err := update()
		if err != nil {
			log.Println(err)
		}
	})

	return
}

func (b *Bidders) Bidders(ids []string) (result []*Agent) {
	m := b.state.Load()
	if m == nil {
		return
	}

	agents := m.(map[string]*Agent)

	result = make([]*Agent, len(ids))
	for i := range ids {
		result[i] = agents[ids[i]]
	}

	return
}

func Import(pattern string) (result []*Agent, err error) {
	matches, err := filepath.Glob(pattern)
	if err != nil {
		return
	}

	parse := func(filename string) (agent *Agent, err error) {
		content, err := ioutil.ReadFile(filename)
		if err != nil {
			return
		}

		// ask Michael to remove leading '0'
		//content = bytes.Replace(content, []byte(": 0"), []byte(": "), -1)

		agent = &Agent{
			content: content,
		}

		err = json.Unmarshal(content, agent)
		if err != nil {
			err = fmt.Errorf("failed to parse: %s (%s)", filename, err.Error())
		}

		return
	}

	for _, filename := range matches {
		var item *Agent
		item, err = parse(filename)
		if err != nil {
			return
		}

		result = append(result, item)
	}

	return
}

type Rules struct {
	ID    string
	Items map[string][]string
}

func (r *Rules) Add(key, value string) {
	if nil == r.Items {
		r.Items = make(map[string][]string)
	}

	r.Items[key] = append(r.Items[key], value)
}

type Filters struct {
	Hash map[string][]string
	Keys []string
}

func (f *Filters) Add(rules *Rules) {
	if nil == f.Hash {
		f.Hash = make(map[string][]string)
	}

	p := make([]string, 0, len(rules.Items))
	for k := range rules.Items {
		p = append(p, k)
	}

	sort.Strings(p)
	f.Keys = p

	f.permute("", p[0], p[1:], rules)
}

/*
func (f *Filters) Get(m map[string]Finder) []string {
	b := make([]byte, 0, 64)

	for i, key := range f.Keys {
		q, ok := m[key]
		if !ok {
			continue
		}

		k := q.Find()
		if k == nil {
			continue
		}

		item := k.(fmt.Stringer)
		if i != 0 {
			io.WriteString(w, ";")
		}

		io.WriteString(w, item.String())
		io.WriteString(w, "=")
		io.WriteString(w, item.String())
	}
}
*/

func (f *Filters) permute(key, head string, tail []string, r *Rules) {
	if len(tail) == 0 {
		for _, value := range r.Items[head] {
			k := key + head + "=" + value
			f.Hash[k] = append(f.Hash[k], r.ID)
		}
	} else {
		for _, value := range r.Items[head] {
			f.permute(key+head+"="+value+";", tail[0], tail[1:], r)
		}
	}
}

func MakeFilters(agents []*Agent) *Filters {
	f := new(Filters)

	for _, agent := range agents {
		rules := &Rules{
			ID: agent.Account[0],
		}

		if agent.DeviceType != nil {
			for _, i := range agent.DeviceType.Include {
				rules.Add("device.type", fmt.Sprintf("%d", i))
			}

			if len(agent.DeviceType.Exclude) != 0 {
				log.Println("device-type filter has excludes")
			}
		}

		if agent.DMA != nil {
			for _, i := range agent.DMA.Include {
				rules.Add("geo.dma", i)
			}

			if len(agent.DMA.Exclude) != 0 {
				log.Println("dma has filter excludes")
			}
		}

		f.Add(rules)
	}

	return f
}

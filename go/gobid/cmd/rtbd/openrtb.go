package main

import (
	"fmt"
	"hash/fnv"
	"io"
	"io/ioutil"
	"log"
	"net/http"
	"strconv"
	"strings"

	"github.com/datacratic/gojq"
	"github.com/datacratic/gometrics/trace"
	"github.com/datacratic/goredis/redis"
	"golang.org/x/net/context"

	"../../rtb"
	"../../rtb/ext/forensiq"
)

type Exchange struct {
	Bidders *Bidders
	Client  *forensiq.Client
	UserIDs *redis.Client
	Exelate *Exelate
}

/*
var jsons = sync.Pool{
	New: func() interface{} { return &jq.Value{} },
}
*/

func (e *Exchange) ServeHTTP(ctx context.Context, w http.ResponseWriter, r *http.Request) {
	ctx = trace.Enter(ctx, "OpenRTB")

	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		trace.Error(ctx, "Errors.Read", err)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	value := &jq.Value{}

	if err := value.Unmarshal(body); err != nil {
		trace.Error(ctx, "Errors.Unmarshal", err)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	p := value.NewQuery()
	impid, err := p.String("imp", "@0", "id")
	if err != nil {
		trace.Error(ctx, "Errors.MissingImpressionID", err)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	q := value.NewQuery()
	if err := q.FindObject("imp", "@0", "ext", "creative-ids"); err != nil {
		trace.Error(ctx, "Errors.MissingIDs", err)
		w.WriteHeader(http.StatusNoContent)
		return
	}

	allowed := make(map[string][]string)
	if q.Down() {
		for {
			if q.Down() {
				list := make([]string, 0, q.Count())
				for {
					list = append(list, q.Value())
					if q.Next() == false {
						break
					}
				}

				q.Up()
				allowed[q.Key()] = list
			}

			if q.Next() == false {
				break
			}
		}
	}

	if len(allowed) == 0 {
		trace.Leave(ctx, "NoAllowedBidders")
		w.WriteHeader(http.StatusNoContent)
		return
	}

	ids := make([]string, 0, len(allowed))
	for i := range allowed {
		ids = append(ids, i)
	}

	bidders := e.Bidders.Bidders(ids)
	bidders = e.Filter(ctx, value, bidders)

	bestPrice := ""
	bestPriority := ""
	best := -1
	for i := range bidders {
		if bidders[i] == nil {
			continue
		}

		price, priority := bidders[i].Bid(ctx)
		if price == "" {
			continue
		}

		if best != -1 {
			if len(priority) < len(bestPriority) {
				continue
			}

			if len(priority) == len(bestPriority) && priority < bestPriority {
				continue
			}

			if priority == bestPriority {
				if len(price) < len(bestPrice) {
					continue
				}

				if len(price) == len(bestPrice) && price < bestPrice {
					continue
				}
			}
		}

		bestPrice = price
		bestPriority = priority
		best = i
	}

	if best == -1 {
		trace.Leave(ctx, "NoAllowedBidders")
		w.WriteHeader(http.StatusNoContent)
		return
	}

	text := `{"seatbid":[{"bid":[{"impid":"%s","price":%f,"crid":"%s","ext":{"priority":%s,"external-id":%s}}]}]}`

	cpm := 0.0
	if money, err := strconv.Atoi(strings.TrimSuffix(bestPrice, "USD/1M")); err != nil {
		trace.Error(ctx, "Errors.Price", err)
		w.WriteHeader(http.StatusNoContent)
		return
	} else {
		cpm = float64(money) / 1000.0
	}

	cid := ids[best]

	augmenters := bidders[best].Augmenters
	if augmenters != nil {
		if f := augmenters.Forensiq; e.Client != nil && f != nil {
			r := &rtb.Components{}
			r.Attach("fields", value)

			score := e.forensiqRiskScore(ctx, r)
			trace.Record(ctx, "Score", score)

			if score > f.Configuration.RiskScore {
				trace.Leave(ctx, "ForensiqRiskScore")
				w.WriteHeader(http.StatusNoContent)
				return
			}
		}
	}

	crid := allowed[cid][0]

	b := string(body)
	if strings.Contains(b, cid) == false {
		log.Println(b)
		log.Println(allowed, ids, cid, crid, best)
	}

	w.Header().Set("Content-Type", "application/json")
	if _, err := fmt.Fprintf(w, text, impid, cpm, crid, bestPriority, cid); err != nil {
		trace.Error(ctx, "Errors.Response", err)
		return
	}

	//jsons.Put(value)

	trace.Leave(ctx, "Responded")
}

func (e *Exchange) forensiqRiskScore(ctx context.Context, r rtb.Request) (value float64) {
	ctx = trace.Enter(ctx, "Forensiq")

	result, err := e.Client.NewRequest(r)
	if err != nil {
		trace.Error(ctx, "Error.NewRequest", err)
		return
	}

	p := result.(rtb.Processor)

	if err := p.Process(ctx, result); err != nil {
		trace.Error(ctx, "Error.Process", err)
		return
	}

	resp := result.Component("forensiq")
	if resp == nil {
		trace.Error(ctx, "Error.NoResponse", err)
		return
	}

	extractor, ok := resp.(rtb.Extractor)
	if !ok {
		trace.Error(ctx, "Error.NoExtractor", err)
		return
	}

	score := extractor.Extract("riskScore")
	if score == nil {
		trace.Leave(ctx, "Error.NoRiskScore")
		return
	}

	value, ok = score.(float64)
	if !ok {
		trace.Leave(ctx, "Error.NoRiskScoreValue")
		return
	}

	trace.Leave(ctx, "Requested")
	return
}

func (e *Exchange) Filter(ctx context.Context, value *jq.Value, bidders []*Agent) (result []*Agent) {
	ctx = trace.Enter(ctx, "Filter")

	result = bidders

	p := value.NewQuery()
	exchange, err := p.String("ext", "exchange")
	if err != nil {
		trace.Leave(ctx, "NoExchange")
		return
	}

	text := ""
	switch exchange {
	case "adaptv":
		text = "ap"
	default:
		trace.Leave(ctx, "NoIDDB")
		return
	}

	q := value.NewQuery()
	id, err := q.String("user", "id")
	if err != nil {
		trace.Leave(ctx, "NoUserID")
		return
	}

	hash := func(s string) []byte {
		h := fnv.New64()
		io.WriteString(h, s)
		return h.Sum([]byte{})
	}

	id = text + ":" + id

	r, err := e.UserIDs.Do("GET", hash(id))
	if err != nil {
		trace.Error(ctx, "NoREDIS", err)
		return
	}

	if r == nil {
		trace.Leave(ctx, "NoID")
		return
	}

	s, ok := r.([]byte)
	if !ok {
		trace.Leave(ctx, "NoStringID")
		return
	}

	_, err = strconv.ParseInt(string(s), 10, 64)
	if err != nil {
		trace.Error(ctx, "BadInt64")
	}

	//if e.Exelate != nil {
	//result = e.Exelate.Filter(ctx, uid, bidders)
	//}

	trace.Leave(ctx, "Found")
	return
}

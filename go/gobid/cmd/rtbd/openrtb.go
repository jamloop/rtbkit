package main

import (
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"strconv"
	"strings"

	"github.com/datacratic/gobid/rtb"
	"github.com/datacratic/gobid/rtb/ext/forensiq"
	"github.com/datacratic/gojq"
	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

type Exchange struct {
	Bidders *Bidders
	Client  *forensiq.Client
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

package rtb

import (
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"net/http"
	"sync/atomic"
	"time"

	"github.com/datacratic/gometrics/defaults"
	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

// ErrNoExtractor indicates that the request doesn't
var ErrNoExtractor = errors.New("no field extractor component")

type Client struct {
	// Client contains the HTTP client used to perform requests.
	Client *http.Client
	// MaximumInFlight contains the maximum allowed number of concurrent requests.
	// There's no maximum if 0.
	MaximumInFlight int64
	// FailCount contains the number of failures that will mark the endpoint as unhealthy.
	// Normal operation will resume when the periodic health check is successful.
	// Defaults to 10 if 0.
	FailCount int64
	// CheckPeriod sets the period used to query the health endpoint.
	// Defaults to 10s.
	CheckPeriod time.Duration

	count    int64
	inflight int64
	tick     chan struct{}
}

// Ready returns the known status of the endpoint.
func (c *Client) Ready() bool {
	return atomic.LoadInt64(&c.count) > 0
}

func (c *Client) MonitorHealth(url string) {
	dt := defaults.Duration(c.CheckPeriod, 10*time.Second)

	c.tick = PeriodicFunc(Every(dt), func() {
		c.HealthCheck(url)
	})
}

func (c *Client) HealthCheck(url string) (err error) {
	r, err := defaults.Client(c.Client).Get(url)
	if err != nil {
		atomic.StoreInt64(&c.count, 0)
		//trace.Error(ctx, "Errors.Fail", err)
		log.Println("forensiq:", err)
		return
	}

	body, err := ioutil.ReadAll(r.Body)
	if err != nil {
		atomic.StoreInt64(&c.count, 0)
		//trace.Error(ctx, "Errors.Down", err)
		log.Println("forensiq: down")
		return
	}

	if result := string(body); r.StatusCode != http.StatusOK || result[0] != '1' {
		atomic.StoreInt64(&c.count, 0)
		err = fmt.Errorf("%s: %s", r.Status, result)
		//trace.Error(ctx, "Errors.Status", err)
		log.Println("forensiq:", err)
		return
	}

	atomic.StoreInt64(&c.count, defaults.Int64(c.FailCount, 10))
	return
}

func (c *Client) Get(ctx context.Context, url string) (r *http.Response, err error) {
	ctx = trace.Enter(ctx, "HTTP")

	if maximum := c.MaximumInFlight; maximum != 0 {
		defer atomic.AddInt64(&c.inflight, -1)

		// too many in-flight?
		if n := atomic.AddInt64(&c.inflight, 1); n >= maximum {
			trace.Leave(ctx, "Errors.TooManyInFlight")
			return
		}
	}

	r, err = defaults.Client(c.Client).Get(url)
	if err != nil {
		atomic.AddInt64(&c.count, -1)
		trace.Error(ctx, "Errors.Fail", err)
		return
	}

	if r.StatusCode != http.StatusOK && r.StatusCode != http.StatusNoContent {
		atomic.AddInt64(&c.count, -1)
		trace.Error(ctx, "Errors.Status", fmt.Errorf("%s", r.Status))
		return
	}

	trace.Leave(ctx, "Check")
	return
}

package forensiq

import (
	"errors"
	"fmt"
	"net/url"
	"sync"

	"github.com/datacratic/gojq"
	"github.com/datacratic/gometrics/defaults"
	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"

	"../../../rtb"
)

type request struct {
	rtb.Components

	client *Client
	values url.Values
}

// Process executes the client's request.
func (req *request) Process(ctx context.Context, r rtb.Request) error {
	return req.client.Process(ctx, r)
}

// ErrMissingFields indicates that required fields are missing from the request.
// The required fields are 'ip' and 'seller'.
// Note that 'rt' is also required and set to 'display' by default.
var ErrMissingFields = errors.New("Forensiq request is missing required fields")

// ErrUnavailable indicates that the Forensiq API is down.
// When down, requests will not be sent until there is a successful health check.
var ErrUnavailable = errors.New("Forensiq API is unavailable")

// Client implements the Forensiq API client.
// Typical uses require setting the client key and fields for extraction.
type Client struct {
	// URL contains the URL used to send requests the Forensiq API.
	// Defaults to http://api.forensiq.com
	URL string
	// ClientKey contains the client key supplied by Forensiq.
	ClientKey string
	// Fields contains information about the data to be extracted from the request.
	// The keys of the map are names of Forensiq input criterias while their value is the path of the field to extract.
	// Requests require a value for the 'ip' and the 'seller' fields.
	// Please refer to the Forensiq documentation for the list of all supported fields.
	Fields map[string][]string
	// Source contains the name of the component that will be used to extract fields.
	// Defaults to 'fields'.
	Source string
	// Target contains the name of the component that will hold the result.
	// Defaults to 'forensiq'.
	Target string
	// HTTP contains details about the HTTP client.
	HTTP rtb.Client
	// Caching indicates if caching is enabled.
	Caching bool

	mu    sync.RWMutex
	cache map[string]*jq.Value
}

// Start installs the health monitor of the Forensiq API.
func (c *Client) Start() error {
	url := defaults.String(c.URL, "http://api.forensiq.com") + "/ready"
	c.HTTP.MonitorHealth(url)
	return nil
}

// HealthCheck sends a request to query the /ready endpoint.
func (c *Client) HealthCheck() error {
	url := defaults.String(c.URL, "http://api.forensiq.com") + "/ready"
	return c.HTTP.HealthCheck(url)
}

// NewRequest creates a request to query the Forensiq API.
func (c *Client) NewRequest(r rtb.Request) (result rtb.Request, err error) {
	args, err := c.prepare(r)
	if err != nil {
		return
	}

	result = &request{
		client: c,
		values: args,
	}

	return
}

// Process sends a request to query the /check endpoint.
// The response is attached to the request.
func (c *Client) Process(ctx context.Context, r rtb.Request) (err error) {
	ctx = trace.Enter(ctx, "Forensiq")

	// ready?
	if !c.HTTP.Ready() {
		err = ErrUnavailable
		trace.Error(ctx, "Errors.Ready", err)
		return
	}

	var args url.Values

	// fast path in case the request is already prepared
	if p, ok := r.(*request); ok {
		args = p.values
	} else {
		args, err = c.prepare(r)
		if err != nil {
			trace.Error(ctx, "Errors.Prepare", err)
			return
		}
	}

	qs := args.Encode()

	// look into the memory cache or perform the query
	value := c.fromCache(qs)
	if value != nil {
		trace.Count(ctx, "CacheHit", 1)
	} else {
		value, err = c.query(ctx, qs)
		if err != nil {
			trace.Error(ctx, "Errors.Request", err)
			return
		}

		c.intoCache(qs, value)
	}

	// hold the result
	r.Attach(defaults.String(c.Target, "forensiq"), value)

	trace.Leave(ctx, "Check")
	return
}

func (c *Client) query(ctx context.Context, qs string) (value *jq.Value, err error) {
	url := defaults.String(c.URL, "http://api.forensiq.com") + "/check?" + qs

	// perform the request
	resp, err := c.HTTP.Get(ctx, url)
	if err != nil {
		return
	}

	// and parse the response back
	value = new(jq.Value)
	err = value.UnmarshalFrom(resp.Body)
	return
}

func (c *Client) fromCache(qs string) (value *jq.Value) {
	if !c.Caching {
		return
	}

	c.mu.RLock()
	value = c.cache[qs]
	c.mu.RUnlock()

	return
}

func (c *Client) intoCache(qs string, value *jq.Value) {
	if !c.Caching {
		return
	}

	c.mu.Lock()

	if c.cache == nil {
		c.cache = make(map[string]*jq.Value)
	}

	c.cache[qs] = value
	c.mu.Unlock()
}

func (c *Client) prepare(r rtb.Request) (result url.Values, err error) {
	extractor, ok := r.Component(defaults.String(c.Source, "fields")).(rtb.Extractor)
	if !ok {
		err = rtb.ErrNoExtractor
		return
	}

	args := url.Values{
		"ck":     []string{c.ClientKey},
		"output": []string{"json"},
		"rt":     []string{"display"},
	}

	extract := func(key string, value []string) {
		item := extractor.Extract(value...)
		if item == nil {
			return
		}

		args.Set(key, fmt.Sprintf("%v", item))
	}

	// extract fields
	for key, value := range c.Fields {
		extract(key, value)
	}

	// valid?
	fields := []string{"ck", "rt", "ip", "seller"}
	for i := range fields {
		items := args[fields[i]]
		if len(items) == 0 || items[0] == "" {
			err = ErrMissingFields
			return
		}
	}

	result = args
	return
}

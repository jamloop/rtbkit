package main

import (
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"log"
	"net"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"sync/atomic"
	"time"

	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

type NoExchange struct {
	URL      string
	Client   *http.Client
	inflight int64
	random   int64
}

var client = &http.Client{
	Transport: &http.Transport{
		MaxIdleConnsPerHost: 1024,
	},
}

var ErrBadIP = errors.New("bad ip")

type Parser interface {
	Parse(string) (interface{}, error)
}

type ParseString struct {
}

func (p *ParseString) Parse(text string) (result interface{}, err error) {
	result = text
	return
}

type ParseNumber struct {
}

func (p *ParseNumber) Parse(text string) (result interface{}, err error) {
	f, err := strconv.ParseFloat(text, 64)
	if err != nil {
		return
	}

	result = f
	return
}

type ParseIP struct {
}

func (p *ParseIP) Parse(text string) (result interface{}, err error) {
	ip := net.ParseIP(text)
	if ip == nil {
		err = ErrBadIP
		return
	}

	result = ip
	return
}

var parameters = map[string]Parser{
	"width":        &ParseNumber{},
	"height":       &ParseNumber{},
	"ip":           &ParseIP{},
	"ua":           &ParseString{},
	"devicetype":   &ParseString{},
	"lang":         &ParseString{},
	"pageurl":      &ParseString{},
	"app_storeurl": &ParseString{},
	"app_bundle":   &ParseString{},
	"appName":      &ParseString{},
	"videotype":    &ParseString{},
	"deviceid":     &ParseString{},
	"partner":      &ParseString{},
	"userid":       &ParseString{},
	"pubid":        &ParseString{},
	"referurl":     &ParseString{},
	"lat":          &ParseNumber{},
	"lon":          &ParseNumber{},
	"price":        &ParseNumber{},
	"idfa":         &ParseString{},
	"idfa_md5":     &ParseString{},
	"idfa_sha1":    &ParseString{},
	"aid":          &ParseString{},
	"aid_md5":      &ParseString{},
	"aid_sha1":     &ParseString{},
}

func (e *NoExchange) ServeHTTP(ctx context.Context, w http.ResponseWriter, r *http.Request) {
	ctx = trace.Enter(ctx, "Request")

	vast := func() {
		value := []byte(`<VAST xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="vast.xsd" version="2.0"/>`)
		w.Header().Set("Content-Type", "application/xml")
		w.Header().Set("Content-Length", fmt.Sprintf("%d", len(value)))
		w.Write(value)
	}

	u := e.URL
	if list := strings.Split(u, ","); len(list) > 1 {
		n := atomic.AddInt64(&e.random, 1)
		u = list[n%int64(len(list))]
	}

	req, err := http.NewRequest("GET", u+r.URL.Path, nil)
	if err != nil {
		trace.Error(ctx, "Errors.Request", err)
		vast()
		return
	}

	if _, err := url.QueryUnescape(r.URL.RawQuery); err != nil {
		log.Println(r.URL.RawQuery)
		trace.Error(ctx, "Errors.BadQueryString", err)
		vast()
		return
	}

	values := r.URL.Query()

	for k, v := range values {
		p, ok := parameters[k]
		if !ok || len(v) != 1 || v[0] == "" {
			delete(values, k)
			continue
		}

		if _, err := p.Parse(v[0]); err != nil {
			delete(values, k)
			continue
		}
	}

	values.Set("id", NewUUID())

	req.URL.RawQuery = values.Encode()

	n := atomic.AddInt64(&e.inflight, 1)
	if n > 32 {
		atomic.AddInt64(&e.inflight, -1)
		trace.Leave(ctx, "Errors.TooManyInFlight")
		vast()
		return
	}

	result := make(chan func() (*http.Response, error), 1)
	go func() {
		c := e.Client
		if nil == c {
			c = client
		}

		resp, err := c.Do(req)
		result <- func() (*http.Response, error) {
			return resp, err
		}

		atomic.AddInt64(&e.inflight, -1)
	}()

	select {
	case <-time.After(50 * time.Millisecond):
		trace.Leave(ctx, "Errors.Timeout")
		vast()

		go func() {
			f := <-result

			resp, err := f()
			if err != nil {
				//trace.Error(ctx, "Errors.TimeoutFailed", err)
				return
			}

			io.Copy(ioutil.Discard, resp.Body)
			resp.Body.Close()
		}()

		return
	case f := <-result:
		resp, err := f()
		if err != nil {
			trace.Error(ctx, "Errors.Failed", err)
			vast()
			return
		}

		h := w.Header()
		for k, v := range resp.Header {
			h[k] = v
		}

		if _, err := io.Copy(w, resp.Body); err != nil {
			trace.Error(ctx, "Errors.Copy", err)
			vast()
			return
		}

		resp.Body.Close()
	}

	trace.Leave(ctx, "Done")
	return
}

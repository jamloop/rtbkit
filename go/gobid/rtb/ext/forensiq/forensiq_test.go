package forensiq

import (
	"fmt"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/datacratic/gobid/rtb"
	"github.com/datacratic/gojq"
	"golang.org/x/net/context"
)

func TestReady(t *testing.T) {
	i := 0

	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != "GET" || r.URL.Path != "/ready" {
			t.Fail()
		}

		fmt.Fprintf(w, "%d", i)
		i++
	}))

	defer s.Close()

	c := &Client{
		URL: s.URL,
	}

	if c.HTTP.Ready() {
		t.Fatal("should not be ready")
	}

	c.HealthCheck()

	if c.HTTP.Ready() {
		t.Fatal("should not be ready")
	}

	c.HealthCheck()

	if !c.HTTP.Ready() {
		t.Fatal("should be ready")
	}
}

func TestCheck(t *testing.T) {
	s := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method == "GET" && r.URL.Path == "/ready" {
			w.Write([]byte("1"))
			return
		}

		if r.Method == "GET" && r.URL.Path == "/check" {
			w.Write([]byte(`{"suspect":true,"timeMs":1,"riskScore":65}`))
			return
		}

		t.Fail()
	}))

	defer s.Close()

	c := &Client{
		URL: s.URL,
	}

	r := &rtb.Components{}

	if result, err := c.NewRequest(r); err == nil || result != nil {
		t.Fatal("endpoint not ready")
	}

	c.HealthCheck()

	if result, err := c.NewRequest(r); err == nil || result != nil {
		t.Fatal("should be missing extractor")
	}

	text := []byte(`{"ip4":"1.2.3.4","pubid":"1234"}`)
	j := &jq.Value{}
	j.Unmarshal(text)
	r.Attach("fields", j)

	if result, err := c.NewRequest(r); err == nil || result != nil {
		t.Fatal("should be missing fields")
	}

	c.Fields = map[string][]string{
		"ip":     []string{"ip4"},
		"seller": []string{"pubid"},
	}

	c.ClientKey = "0123456789"

	result, err := c.NewRequest(r)
	if err != nil {
		t.Fatal(err)
	}

	p := result.(rtb.Processor)

	if err := p.Process(context.Background(), result); err != nil {
		t.Fatal(err)
	}
}

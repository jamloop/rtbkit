package rtb

import (
	"net/http"
	"testing"
	"time"

	"golang.org/x/net/context"
)

func TestServerClose(t *testing.T) {
	s := Server{
		Server: http.Server{Addr: ":0"},
	}

	s.Close()
}

func TestServerStartClose(t *testing.T) {
	s := Server{
		Server: http.Server{Addr: ":0"},
	}

	if err := s.Start(); err != nil {
		t.Fatal(err)
	}

	s.Close()
}

func TestServerStartReadyClose(t *testing.T) {
	s := Server{
		Server: http.Server{Addr: ":0"},
	}

	if err := s.Start(); err != nil {
		t.Fatal(err)
	}

	r, err := http.Get(s.URL() + "/ready")
	if err != nil {
		t.Fatal(err)
	}

	if code := r.StatusCode; code != http.StatusOK {
		t.Fatal(code)
	}

	s.Close()
}

func TestServerHandler(t *testing.T) {
	s := Server{
		Server: http.Server{Addr: ":0"},
		Handler: HandlerFunc(func(ctx context.Context, w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusNoContent)
		}),
		Timeout: time.Second,
	}

	if err := s.Start(); err != nil {
		t.Fatal(err)
	}

	r, err := http.Get(s.URL())
	if err != nil {
		t.Fatal(err)
	}

	if code := r.StatusCode; code != http.StatusNoContent {
		t.Fatal(code)
	}

	s.Close()
}

func TestServerHandlerTimeout(t *testing.T) {
	c := make(chan struct{})

	s := Server{
		Server: http.Server{Addr: ":0"},
		Handler: HandlerFunc(func(ctx context.Context, w http.ResponseWriter, r *http.Request) {
			w.WriteHeader(http.StatusNoContent)
			close(c)
			time.Sleep(time.Minute)
		}),
		Timeout: time.Second,
	}

	if err := s.Start(); err != nil {
		t.Fatal(err)
	}

	go func() {
		r, err := http.Get(s.URL())
		if err != nil {
			t.Fatal(err)
		}

		if code := r.StatusCode; code != http.StatusNoContent {
			t.Fatal(code)
		}
	}()

	<-c

	s.Close()
}

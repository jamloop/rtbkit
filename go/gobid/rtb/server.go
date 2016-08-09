package rtb

import (
	"fmt"
	"io"
	"net"
	"net/http"
	"sync"
	"sync/atomic"
	"time"

	"github.com/datacratic/gometrics/defaults"
	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

// Handler adds a context to the http.Handler interface.
type Handler interface {
	ServeHTTP(context.Context, http.ResponseWriter, *http.Request)
}

// Server is a thin abstraction around the HTTP server that provides context to handlers.
// It also handles graceful shutdown.
// When closed, the server will wait until all connections are closed.
// If a connection is idle, it relies on the read timeout to close the connection.
// Lastly, it will close the handler if it implements io.Closer.
type Server struct {
	// Server contains details on the underlying HTTP server.
	// Note that the ReadTimeout must be set when using graceful shutdown.
	// Will be one minute if 0.
	Server http.Server
	// Handler serves HTTP requests.
	Handler Handler
	// Tracer will handle trace events.
	Tracer trace.Handler
	// Name defines the name of the root context.
	// Will use Server.Addr if empty.
	Name string
	// Timeout defines the maximum amount of time allowed to close connections.
	// Will be one minute if 0.
	Timeout time.Duration

	listen net.Listener
	wg     sync.WaitGroup
	name   string
	ready  int64
	count  int64
	tick   chan struct{}
	mu     sync.Mutex
	conns  map[net.Conn]http.ConnState
}

// ServeHTTP creates the root context and pass it to the HTTP request handler.
// The default /ready route is also handled.
func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if atomic.LoadInt64(&s.ready) == 0 {
		http.Error(w, "server is closed", http.StatusServiceUnavailable)
		return
	}

	// start trace
	key := r.Header.Get(trace.HeaderKey)
	ctx := trace.Start(trace.SetHandler(context.Background(), s.Tracer), s.name, key)

	// ready?
	if r.URL.Path == "/ready" && r.Method == "GET" {
		trace.Leave(ctx, "Ready")
		w.WriteHeader(http.StatusOK)
		return
	}

	if s.Handler != nil {
		s.Handler.ServeHTTP(ctx, w, r)
	}

	trace.Leave(ctx, "Served")
}

// Start installs the server and starts serving requests until the server is closed.
func (s *Server) Start() (err error) {
	s.Server.Handler = s

	// open socket
	s.listen, err = net.Listen("tcp", defaults.String(s.Server.Addr, ":http"))
	if err != nil {
		return
	}

	s.name = defaults.String(s.Name, s.listen.Addr().String())

	// provide a default read timeout if missing
	if 0 == s.Server.ReadTimeout {
		s.Server.ReadTimeout = time.Minute
	}

	// track new/closed HTTP connections
	s.conns = make(map[net.Conn]http.ConnState)

	cs := s.Server.ConnState
	s.Server.ConnState = func(conn net.Conn, state http.ConnState) {
		s.mu.Lock()
		defer s.mu.Unlock()

		switch state {
		case http.StateNew:
			atomic.AddInt64(&s.count, +1)
			s.wg.Add(1)
			s.conns[conn] = state
		case http.StateClosed, http.StateHijacked:
			atomic.AddInt64(&s.count, -1)
			s.wg.Done()
			delete(s.conns, conn)
		case http.StateActive, http.StateIdle:
			s.conns[conn] = state
		}

		if nil != cs {
			cs(conn, state)
		}
	}

	// update metrics
	s.tick = Tick(time.Second, func() {
		ctx := trace.Start(trace.SetHandler(context.Background(), s.Tracer), s.name+".Tick", "")
		trace.Set(ctx, "Connections", atomic.LoadInt64(&s.count))
		trace.Set(ctx, "State", atomic.LoadInt64(&s.ready))
		trace.Leave(ctx, "Ticked")
	})

	// serve requests
	go func() {
		s.Server.Serve(s.listen)
	}()

	// set ready
	if !atomic.CompareAndSwapInt64(&s.ready, 0, 1) {
		panic("server is already ready to serve requests")
	}

	return
}

// URL returns the address of the HTTP server.
func (s *Server) URL() string {
	return "http://" + s.listen.Addr().String()
}

// Close gracefully shutdowns the HTTP server.
func (s *Server) Close() (err error) {
	if !atomic.CompareAndSwapInt64(&s.ready, 1, 0) {
		return
	}

	// close active connections
	s.Server.SetKeepAlivesEnabled(false)

	// close the listen socket (ignore any errors)
	s.listen.Close()

	// close idle connections
	s.mu.Lock()
	for conn, state := range s.conns {
		if state == http.StateIdle || state == http.StateNew {
			conn.Close()
		}
	}

	s.mu.Unlock()

	// wait for all connections to be closed
	done := make(chan struct{})
	go func() {
		s.wg.Wait()

		// we're now safe to close the handler
		if closer, ok := s.Handler.(io.Closer); ok {
			err = closer.Close()
		}

		close(done)
	}()

	// make sure we timeout
	timeout := defaults.Duration(s.Timeout, time.Minute)
	select {
	case <-done:
	case <-time.After(timeout):
		err = fmt.Errorf("timeout closing after %v", timeout)
	}

	close(s.tick)
	return
}

// HandlerFunc defines an helper to support the Handler interface.
type HandlerFunc func(context.Context, http.ResponseWriter, *http.Request)

// ServerHTTP invokes the function literal to server the HTTP request.
func (f HandlerFunc) ServeHTTP(ctx context.Context, w http.ResponseWriter, r *http.Request) {
	f(ctx, w, r)
}

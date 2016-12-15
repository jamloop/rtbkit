package main

import (
	"flag"
	"log"
	"net/http"
	"os"
	"os/signal"
	"syscall"

	"gitlab.com/ericrobert/goredis/redis"

	_ "expvar"
	_ "net/http/pprof"

	"github.com/datacratic/gometrics/trace"

	"../../defaults"
	"../../rtb"
	"../../rtb/ext/forensiq"
)

func main() {
	log.SetFlags(log.Lshortfile)

	name := flag.String("name", defaults.Name(), "server name; will use fqdn (reversed) by default")
	private := flag.String("private", ":6060", "server address of the private/debug endpoint")

	flag.Parse()

	t := trace.New()

	bidders := &Bidders{
		Pattern: "/home/rtbkit/prod/rtb/configs/bidders/*.json",
		Tracer:  t,
		Name:    *name + ".Bidders",
	}

	f := forensiq.Client{
		ClientKey: "On7GnjI4WfbtLf1WDp3X",
		Fields: map[string][]string{
			"url":    []string{"site", "page"},
			"ip":     []string{"device", "ip"},
			"ua":     []string{"device", "ua"},
			"seller": []string{"ext", "exchange"},
		},
	}

	f.Start()

	s := rtb.Services{
		&rtb.Server{
			Server: http.Server{Addr: ":9175"},
			Handler: &NoExchange{
				URL: "http://127.0.0.1:9975,http://127.0.0.1:19975",
			},
			Name:   *name + ".9175",
			Tracer: t,
		},
		&rtb.Server{
			Server: http.Server{Addr: ":9176"},
			Handler: &Exchange{
				Bidders: bidders,
				Client:  &f,
				UserIDs: &redis.Client{
					Address: []string{"tcp://172.31.8.117:6479"},
				},
				Exelate: &Exelate{
					Days: 30,
				},
			},
			Name:   *name + ".9176",
			Tracer: t,
		},
		bidders,
	}

	log.Println("starting", *name)

	if err := s.Start(); err != nil {
		log.Println(err)
	}

	// default HTTP server is private
	go func() {
		log.Fatal(http.ListenAndServe(*private, nil))
	}()

	// wait on signal handler for graceful shutdown
	signals := make(chan os.Signal, 1)
	signal.Notify(signals, syscall.SIGINT, syscall.SIGTERM)
	<-signals

	log.Println("closing...")

	if err := s.Close(); err != nil {
		log.Fatal(err)
	}

	log.Println("done.")
}

package main

import (
	"bufio"
	"bytes"
	"compress/gzip"
	"encoding/binary"
	"fmt"
	"hash/fnv"
	"io"
	"io/ioutil"
	"log"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/aws/aws-sdk-go/aws"
	"github.com/aws/aws-sdk-go/aws/credentials"
	"github.com/aws/aws-sdk-go/aws/session"
	"github.com/aws/aws-sdk-go/service/s3"
	"github.com/aws/aws-sdk-go/service/s3/s3manager"
	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

type Exelate struct {
	Days int

	data atomic.Value
	list map[int]struct{}
	mu   sync.Mutex
}

func (e *Exelate) Segments(uid uint64, list []int) (result map[int]struct{}) {
	v := e.data.Load()
	if v == nil {
		e.request(list)
		return
	}

	s := v.(*segments)

	if !s.has(list) {
		e.request(list)
	}

	m, ok := s.users[uid]
	if !ok {
		return
	}

	result = m
	return
}

func (e *Exelate) request(list []int) {
	e.mu.Lock()

	if e.list == nil {
		e.list = make(map[int]struct{})
		go e.refresh()
	}

	for _, s := range list {
		e.list[s] = struct{}{}
	}

	e.mu.Unlock()
}

func (e *Exelate) refresh() {
	work := func() {
		list := make([]int, 0, len(e.list))

		e.mu.Lock()

		for k := range e.list {
			list = append(list, k)
		}

		e.mu.Unlock()

		e.load(list)
	}

	work()

	tick := time.Tick(15 * time.Minute)
	for _ = range tick {
		work()
	}
}

func (e *Exelate) load(list []int) {
	s := new(segments)

	days := e.Days
	if 0 == days {
		days = 60
	}

	now := time.Now().UTC()

	for i := 0; i < days; i++ {
		date := now.Format("2006-01-02")
		for _, j := range list {
			name := fmt.Sprintf("%s/%d.gz", date, j)
			body := e.read(name)
			if len(body) != 0 {
				z, err := gzip.NewReader(bytes.NewReader(body))
				if err != nil {
					log.Println(name, err)
					continue
				}

				s.add(j, z)
			}
		}

		now = now.AddDate(0, 0, -1)
	}

	s.list = make(map[int]struct{})
	for _, k := range list {
		s.list[k] = struct{}{}
	}

	e.data.Store(s)
}

type segments struct {
	users map[uint64]map[int]struct{}
	list  map[int]struct{}
}

func (s *segments) has(list []int) bool {
	for _, k := range list {
		_, ok := s.list[k]
		if !ok {
			return false
		}
	}

	return true
}

func (s *segments) add(k int, r io.Reader) {
	if s.users == nil {
		s.users = make(map[uint64]map[int]struct{})
	}

	hash := func(s string) []byte {
		h := fnv.New64()
		io.WriteString(h, s)
		return h.Sum([]byte{})
	}

	sr := bufio.NewScanner(r)
	for sr.Scan() {
		id := binary.BigEndian.Uint64(hash(sr.Text()))

		m, ok := s.users[id]
		if !ok {
			m = make(map[int]struct{})
			s.users[id] = m
		}

		m[k] = struct{}{}
	}
}

func (e *Exelate) read(file string) (body []byte) {
	filename := "./exelate/" + file

	body, err := ioutil.ReadFile(filename)
	if err == nil {
		if len(body) != 0 {
			return
		}
	}

	config := aws.Config{
		Region: aws.String("us-east-1"),
	}

	accessKey := "AKIAJ6GRUH5N4WVHMI6A"
	secretKey := "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"
	config.Credentials = credentials.NewStaticCredentials(accessKey, secretKey, "")

	d := s3manager.NewDownloaderWithClient(s3.New(session.New(), &config))

	args := &s3.GetObjectInput{
		Bucket: aws.String("exelatesegments"),
		Key:    aws.String(file),
	}

	f, err := os.Create(filename)
	if err != nil {
		os.MkdirAll(filepath.Dir(filename), 0755)
		f, err = os.Create(filename)
		if err != nil {
			log.Println(err)
			return
		}
	}

	if _, err = d.Download(f, args); err != nil {
		return
	}

	f.Close()

	body, err = ioutil.ReadFile(filename)
	if err != nil {
		log.Println(filename, err)
	}

	return
}

func (e *Exelate) Filter(ctx context.Context, uid uint64, bidders []*Agent) (result []*Agent) {
	ctx = trace.Enter(ctx, "Exelate")

	list := make([]int, 0)

	for _, b := range bidders {
		s := b.Parameters.Exelate.Segments
		if len(s) != 0 {
			list = append(list, s...)
		}
	}

	m := e.Segments(uid, list)

	for _, b := range bidders {
		s := b.Parameters.Exelate.Segments
		if len(s) == 0 {
			result = append(result, b)
			continue
		}

		if m == nil {
			if !b.Parameters.Exelate.Required {
				result = append(result, b)
			}

			continue
		}

		trace.Count(ctx, b.Account[0]+".Test", 1)

		for _, k := range s {
			if _, ok := m[k]; ok {
				result = append(result, b)
				trace.Count(ctx, b.Account[0]+".Pass", 1)
				break
			}
		}
	}

	trace.Leave(ctx, "Filtered")
	return
}

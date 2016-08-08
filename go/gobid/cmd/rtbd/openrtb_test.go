package main

import (
	"bytes"
	"encoding/json"
	"io/ioutil"
	"log"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/datacratic/gometrics/trace"
	"golang.org/x/net/context"
)

var sample = []byte(`
{
  "app": {
    "bundle": "com.cleanmaster.mguard",
    "content": {
      "language": "[LANG]"
    },
    "name": "clean+master+(300x250)",
    "storeurl": "https://play.google.com/store/apps/details?id=com.cleanmaster.mguard"
  },
  "at": 2,
  "device": {
    "devicetype": 4,
    "ip": "172.56.16.226",
    "ua": "Mozilla/5.0+(Linux;+Android+5.1.1;+LG-K330+Build/LMY47V;+wv)+AppleWebKit/537.36+(KHTML,+like+Gecko)+Version/4.0+Chrome/46.0.2490.76+Mobile+Safari/537.36"
  },
  "ext": {
    "exchange": "publisher",
    "price": 5000,
    "videotype": "instream"
  },
  "id": "0a97fa93-ab2c-de6c-d6d3-96dc50052d16",
  "imp": [
    {
      "ext": {
        "creative-ids": {
          "5162210": [
            5162210
          ]
        },
        "external-ids": [
          5162210
        ]
      },
      "id": "1",
      "video": {
        "pos": 0
      }
    }
  ],
  "tmax": 79,
  "user": {
    "geo": {
      "country": "US",
      "metro": "618",
      "region": "TX",
      "zip": "77036"
    }
  }
}`)

/*
var sample = []byte(`
{
  "user": {
    "ext": {
      "sessiondepth": 1
    },
    "buyeruid": "asdf",
    "id": "edcbabb5691ece950fa9da145ee1cf871183ca4d"
  },
  "device": {
    "geo": {
      "region": "TX",
      "country": "USA"
    },
    "js": 1,
    "devicetype": 2,
    "language": "en",
    "ip": "10.83.4.24",
    "ua": "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_8_3) AppleWebKit/537.31 (KHTML, like Gecko) Chrome/26.0.1410.65 Safari/537.31"
  },
  "site": {
    "ext": {
      "mopt": 0
    },
    "publisher": {
      "id": "2147483647"
    },
    "page": "",
    "cat": [
      "IAB1"
    ],
    "name": "",
    "domain": "discovery.com",
    "id": "14463"
  },
  "imp": [
    {
      "banner": {
        "api": [],
        "battr": [
          9,
          1,
          14014,
          14002,
          8,
          14,
          14019,
          3
        ],
        "h": 600,
        "w": 160
      },
      "tagid": "30027",
      "id": "1",
      "ext": {"creative-ids" : {"1":[1, 2, 3],"2":[1]}}
    }
  ],
  "tmax": 893,
  "at": 2,
  "id": "f4093ca4834dfd750452aba7ce0ca00c2705a80a"
}`)*/

func TestOpenRTB(t *testing.T) {
	m := make(map[string]interface{})

	if err := json.Unmarshal(sample, &m); err != nil {
		t.Fatal(err)
	}

	br, err := json.Marshal(m)
	if err != nil {
		t.Fatal(err)
	}

	log.Println(string(br))

	b := &Bidders{
		Pattern: "../../../../configs/bidders/*.json",
	}

	c := trace.Start(trace.SetHandler(context.Background(), nil), "test", "")

	b.Start()
	e := &Exchange{Bidders: b}

	w := httptest.NewRecorder()
	e.ServeHTTP(c, w, &http.Request{Body: ioutil.NopCloser(bytes.NewReader(br))})

	trace.Leave(c, "done")

	log.Println(w.Code, w.Body.String())
}

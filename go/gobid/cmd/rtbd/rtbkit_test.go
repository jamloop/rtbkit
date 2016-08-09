package main

import "testing"

func TestRTBkit(t *testing.T) {
	agents, err := Import("configs/*.json")
	if err != nil {
		t.Fatal(err)
	}

	/*
		buf, err := json.Marshal(agents)
		if err != nil {
			t.Fatal(err)
		}

		fmt.Println(string(buf))
	*/

	MakeFilters(agents)
}

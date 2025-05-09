package main

import (
	"faissextcode"
	"fmt"
	"os"

	_ "github.com/marcboeker/go-duckdb"
)

func main() {
	db, err := faissextcode.SetupDatabase()
	if err != nil {
		fmt.Println("Unable to setup database:", err)
		return
	}

	_, err = db.Exec("LOAD json")
	if err != nil {
		panic(err)
	}

	_, err = db.Exec("CALL FAISS_LOAD('flat', 'indices/IDMap,HNSW128,Flat.index')")
	if err != nil {
		panic(err)
	}

	_, err = db.Exec("CREATE TABLE queries AS SELECT qid, vector AS embedding FROM 'conformanceTests/anserini-tools/topics-and-qrels/topics.dl19-passage.openai-ada2.jsonl.gz'")
	if err != nil {
		panic(err)
	}
	rows, err := db.Query("SELECT qid, UNNEST(faiss_search('flat', 1000, embedding, MAP{'nprobe': '32'})) FROM queries")
	if err != nil {
		panic(err)
	}

	file, err := os.Create("results")
	if err != nil {
		panic(err)
	}
	var id int
	var x map[string]interface{}
	for rows.Next() {
		rows.Scan(&id, &x)
		if x["label"].(int64) != -1 {
			fmt.Fprintln(file, id, 0, x["label"], x["distance"], x["rank"], "TODO")
		}
	}
}

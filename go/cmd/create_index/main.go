package main

import (
	"faissextcode"
	"fmt"
	"os"

	_ "github.com/marcboeker/go-duckdb"
)

const vectorLength = 1536

func main() {
	if len(os.Args) < 3 {
		fmt.Println("Need at least one argument. The first argument must be the index string for faiss factory. The second argument is the output file")
	}
	indexType := os.Args[1]
	outputFile := os.Args[2]
	db, err := faissextcode.SetupDatabase()
	if err != nil {
		fmt.Println("Unable to setup database:", err)
		return
	}
	_, err = db.Exec("CREATE TABLE input AS SELECT docid, vector FROM 'conformanceTests/msmarco-passage-openai-ada2/*.jsonl.gz'")
	if err != nil {
		fmt.Println("Unable to import msmarco")
		return
	}
	_, err = db.Exec(fmt.Sprintf("CALL FAISS_CREATE('main', %d, '%s')", vectorLength, indexType))
	if err != nil {
		fmt.Println("Unable to create the faiss index")
		return
	}
	_, err = db.Exec("CALL FAISS_MANUAL_TRAIN((SELECT vector FROM input), 'main')")
	if err != nil {
		fmt.Println("Unable to train the faiss index")
		return
	}
	_, err = db.Exec("CALL FAISS_ADD('main', (SELECT docid, vector FROM input))")
	if err != nil {
		fmt.Println("Unable to train the faiss index")
		return
	}
	_, err = db.Exec(fmt.Sprintf("CALL FAISS_SAVE('main', '%s')", outputFile))
	if err != nil {
		fmt.Println("Unable to train the faiss index")
		return
	}
}

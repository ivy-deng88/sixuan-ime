package main

import (
	"os"
	"path/filepath"
	"sync"
	"testing"
	"time"
)

func TestDictionaryConcurrentLearningAndCache(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := learnWord("ni hao", "你好"); err != nil {
				t.Error(err)
			}
		}()
	}
	wg.Wait()
	rows := phraseCandidates("nihao")
	if len(rows) != 1 || rows[0].Weight != 20 {
		t.Fatal(rows)
	}
	if err := learnWord("nihao", "你好呀"); err != nil {
		t.Fatal(err)
	}
	if len(phraseCandidates("nihao")) != 2 {
		t.Fatal("cache not invalidated")
	}
	dir := filepath.Join(os.Getenv("XDG_CONFIG_HOME"), configDirName)
	os.MkdirAll(dir, 0700)
	os.WriteFile(filepath.Join(dir, "phrases.tsv"), []byte("sxkj\t思玄科技\n"), 0600)
	if r := phraseCandidates("sxkj"); len(r) != 1 || r[0].Text != "思玄科技" {
		t.Fatal(r)
	}
	if st, _ := os.Stat(wordsPath("learned-words.json")); st.Mode().Perm() != 0600 {
		t.Fatal("private mode")
	}
}

func TestPhraseRecencyRanking(t *testing.T) {
	t.Setenv("XDG_STATE_HOME", t.TempDir())
	t.Setenv("XDG_CONFIG_HOME", t.TempDir())
	if err := learnWord("shengtu", "圣徒"); err != nil {
		t.Fatal(err)
	}
	// 把圣徒的时间戳拨回 40 天前，模拟久未使用的高频旧词。
	rows := readWords("learned-words.json")
	for i := range rows {
		if rows[i].Text == "圣徒" {
			rows[i].Weight = 19
			rows[i].Last = time.Now().Unix() - 40*86400
		}
	}
	if err := atomicJSON(wordsPath("learned-words.json"), rows); err != nil {
		t.Fatal(err)
	}
	if err := learnWord("shengtu", "生图"); err != nil {
		t.Fatal(err)
	}
	ordered := phraseCandidates("shengtu")
	if len(ordered) != 2 {
		t.Fatal(ordered)
	}
	if ordered[0].Text != "生图" {
		t.Fatalf("recent word should rank first: %+v", ordered)
	}
	if ordered[1].Text != "圣徒" {
		t.Fatalf("stale word should rank second: %+v", ordered)
	}
	// 旧词再次使用后应回到首位：权重优势恢复，衰减只惩罚长期不用。
	if err := learnWord("shengtu", "圣徒"); err != nil {
		t.Fatal(err)
	}
	ordered = phraseCandidates("shengtu")
	if ordered[0].Text != "圣徒" {
		t.Fatalf("used word should rank first again: %+v", ordered)
	}
}

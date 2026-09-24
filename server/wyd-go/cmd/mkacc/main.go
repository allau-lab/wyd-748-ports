package main

import (
	"fmt"
	"os"

	"wydgo/internal/account"
	"wydgo/internal/model"
	"wydgo/internal/store"
)

func main() {
	dir := "/tmp/wydgo-data/accounts"
	user := "gilmar"
	pass := "123456"
	if len(os.Args) > 1 {
		user = os.Args[1]
	}
	if len(os.Args) > 2 {
		pass = os.Args[2]
	}
	if len(os.Args) > 3 {
		dir = os.Args[3]
	}
	_ = os.MkdirAll(dir, 0o755)
	hash, err := account.HashPassword(pass)
	if err != nil {
		panic(err)
	}
	s := store.NewJSONStore(dir)
	acc := &model.Account{Name: user, PasswordHash: hash, Chars: []model.Char{}}
	if err := s.CreateAccount(acc); err != nil {
		if a, e := s.LoadAccount(user); e == nil {
			fmt.Println("exists", a.Name)
			return
		}
		panic(err)
	}
	fmt.Println("created", user)
}

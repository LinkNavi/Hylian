package main
import "fmt"
func main(){ var s int64; for i:=int64(0);i<12000;i++ { for j:=int64(0);j<5000;j++ { s=(s*31+(i^j))%1000003 } }; fmt.Println(s) }

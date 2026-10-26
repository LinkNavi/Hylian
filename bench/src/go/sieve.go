package main
import "fmt"
func main(){ n:=10000000; f:=make([]int64,n+1); var c int64
 for i:=2;i<=n;i++ { if f[i]==0 { c++; for j:=i*2;j<=n;j+=i { f[j]=1 } } }
 fmt.Println(c) }

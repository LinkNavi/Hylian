package main
import "fmt"
func main(){ var best,bestn int64
 for n:=int64(1);n<1000000;n++ { x,st:=n,int64(0); for x!=1 { if x%2==0 {x/=2} else {x=3*x+1}; st++ }; if st>best {best=st;bestn=n} }
 fmt.Println(bestn) }

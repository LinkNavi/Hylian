package main
import "fmt"
func main(){ n:=250; a:=make([]int64,n*n); b:=make([]int64,n*n); c:=make([]int64,n*n)
 for i:=0;i<n;i++ { for j:=0;j<n;j++ { a[i*n+j]=int64((i+j)%10); b[i*n+j]=int64((i*j)%10) } }
 for i:=0;i<n;i++ { for j:=0;j<n;j++ { var s int64; for k:=0;k<n;k++ { s+=a[i*n+k]*b[k*n+j] }; c[i*n+j]=s } }
 var sum int64; for _,v:=range c { sum+=v }; fmt.Println(sum) }

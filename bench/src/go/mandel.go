package main
import "fmt"
func main(){ var total int64
 for py:=0;py<600;py++ { for px:=0;px<600;px++ {
  cr:=float64(px)*3.0/600.0-2.0; ci:=float64(py)*2.0/600.0-1.0; zr,zi:=0.0,0.0; it:=0
  for it<100 && zr*zr+zi*zi<4.0 { t:=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; it++ }
  total+=int64(it) } }
 fmt.Println(total) }

fn main(){ let (mut best,mut bestn)=(0i64,0i64);
 for n in 1..1000000i64 { let (mut x,mut st)=(n,0i64); while x!=1 { if x%2==0 {x/=2} else {x=3*x+1} st+=1; } if st>best {best=st;bestn=n;} }
 println!("{}",bestn); }

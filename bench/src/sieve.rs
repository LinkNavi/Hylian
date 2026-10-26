fn main(){ let n=10000000usize; let mut f=vec![0i64;n+1]; let mut c=0i64;
 for i in 2..=n { if f[i]==0 { c+=1; let mut j=i*2; while j<=n { f[j]=1; j+=i; } } }
 println!("{}",c); }

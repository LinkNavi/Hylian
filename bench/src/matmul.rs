fn main(){ let n=250usize; let mut a=vec![0i64;n*n]; let mut b=vec![0i64;n*n]; let mut c=vec![0i64;n*n];
 for i in 0..n { for j in 0..n { a[i*n+j]=((i+j)%10) as i64; b[i*n+j]=((i*j)%10) as i64; } }
 for i in 0..n { for j in 0..n { let mut s=0i64; for k in 0..n { s+=a[i*n+k]*b[k*n+j]; } c[i*n+j]=s; } }
 let sum:i64=c.iter().sum(); println!("{}",sum); }

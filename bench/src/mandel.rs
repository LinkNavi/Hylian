fn main(){ let mut total:i64=0;
 for py in 0..600 { for px in 0..600 {
  let cr=(px as f64)*3.0/600.0-2.0; let ci=(py as f64)*2.0/600.0-1.0;
  let (mut zr,mut zi,mut it)=(0.0f64,0.0f64,0);
  while it<100 && zr*zr+zi*zi<4.0 { let t=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; it+=1; }
  total+=it as i64; } }
 println!("{}",total); }

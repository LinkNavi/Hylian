fn main(){ let mut s:i64=0; for i in 0..12000i64 { for j in 0..5000i64 { s=(s*31+(i^j))%1000003; } } println!("{}",s); }

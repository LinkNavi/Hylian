def main():
    best=0; bestn=0
    for n in range(1,1000000):
        x=n; st=0
        while x!=1:
            x = x//2 if x%2==0 else 3*x+1
            st+=1
        if st>best: best=st; bestn=n
    print(bestn)
main()

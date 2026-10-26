def main():
    n=250
    a=[0]*(n*n); b=[0]*(n*n); c=[0]*(n*n)
    for i in range(n):
        for j in range(n):
            a[i*n+j]=(i+j)%10; b[i*n+j]=(i*j)%10
    for i in range(n):
        for j in range(n):
            s=0
            for k in range(n): s+=a[i*n+k]*b[k*n+j]
            c[i*n+j]=s
    print(sum(c))
main()

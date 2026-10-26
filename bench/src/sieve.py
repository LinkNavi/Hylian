def main():
    n=10000000; f=[0]*(n+1); c=0
    for i in range(2,n+1):
        if f[i]==0:
            c+=1
            for j in range(i*2,n+1,i): f[j]=1
    print(c)
main()

def main():
    s=0
    for i in range(12000):
        for j in range(5000):
            s=(s*31+(i^j))%1000003
    print(s)
main()

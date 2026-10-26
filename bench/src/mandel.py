def main():
    total=0
    for py in range(600):
        for px in range(600):
            cr=px*3.0/600.0-2.0; ci=py*2.0/600.0-1.0; zr=0.0; zi=0.0; it=0
            while it<100 and zr*zr+zi*zi<4.0:
                t=zr*zr-zi*zi+cr; zi=2.0*zr*zi+ci; zr=t; it+=1
            total+=it
    print(total)
main()

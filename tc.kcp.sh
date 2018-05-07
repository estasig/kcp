
tc(){
    comcast --stop
    sudo pfctl -E
    (cat /etc/pf.conf && echo "dummynet-anchor \"lichess\"" && echo "anchor \"lichess\"") | sudo pfctl -q -f -
    echo "dummynet in quick proto $1 from any to any port $2 pipe 1" | sudo pfctl -q -a lichess -f -
    #echo $'dummynet in all pipe 1' | sudo pfctl -q -a lichess -f -
    sudo dnctl pipe 1 config delay $4 bw $5Kbit/s plr $6

    echo "====================================================="
    printf "$3:$1:$2 ::: dealy $4ms, bw $5Kbit/s, loss $6\n"
    echo "====================================================="
}

main() {
    if [ x"$3" == x"gprs" ]; then
        tc $1 $2 $3 500 50 0.02
    elif [ x"$3" == x"edge" ]; then
        tc $1 $2 $3 300 250 0.015
    elif [ x"$3" == x"3g" ]; then
        tc $1 $2 $3 250 750 0.015
    elif [ x"$3" == x"dial" ]; then
        tc $1 $2 $3 185 40 0.02
    elif [ x"$3" == x"dsl" ]; then
        tc $1 $2 $3 40 8000 0.005
    elif [ x"$3" == x"wifi" ]; then
        tc $1 $2 $3 40 30000 0.002
        #tc $1 40 50000 0.002
    elif [ x"$3" == x"satellite" ]; then
        tc $1 $2 $3 1500 1000000 0.002
    elif [ x"$3" == x"x" ]; then
        tc $1 $2 $3 $4 $5 $6
    else
        printf "unknown network\n"
    fi
}

if [ "$#" -lt 3 ]; then
    echo "usage: $0 tcp/udp port gprs/edge/3g/dial/dsl/wifi/satellite\n"
    exit
fi
main $1 $2 $3 $4 $5 $6

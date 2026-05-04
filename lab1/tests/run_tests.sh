#!/bin/bash

# Script para correr testes de eficiencia do protocolo
# Precisa do cable simulator a correr: ./bin/cable

MAIN="../bin/main"
TX_PORT="/dev/ttyS10"
RX_PORT="/dev/ttyS11"
TEST_FILE="test_file.bin"
RX_FILE="received_file.bin"
CSV="results.csv"
REPEATS=3

# gerar ficheiro de teste (10KB)
dd if=/dev/urandom of=$TEST_FILE bs=1024 count=10 2>/dev/null
FILE_SIZE=$(stat -c%s "$TEST_FILE")
echo "Ficheiro de teste: $FILE_SIZE bytes"

# header do csv
echo "test_name,fer,tprop,frame_size,S_tx,time,repeat" > $CSV

# funcao para correr um teste
run_test() {
    local test_name=$1
    local fer=$2
    local tprop=$3
    local frame_size=$4
    local repeat=$5

    echo "--- $test_name (repeat $repeat) ---"
    echo "    FER=$fer tprop=$tprop frame_size=$frame_size"

    # limpar ficheiro recebido
    rm -f $RX_FILE

    # correr tx e rx em paralelo
    $MAIN $TX_PORT tx $TEST_FILE $fer $tprop $frame_size > /tmp/tx_output.txt 2>&1 &
    TX_PID=$!
    $MAIN $RX_PORT rx $RX_FILE $fer $tprop $frame_size > /tmp/rx_output.txt 2>&1 &
    RX_PID=$!

    # esperar que ambos acabem (timeout de 120s)
    local timeout=120
    local elapsed=0
    while kill -0 $TX_PID 2>/dev/null || kill -0 $RX_PID 2>/dev/null; do
        sleep 1
        elapsed=$((elapsed + 1))
        if [ $elapsed -ge $timeout ]; then
            echo "    TIMEOUT - a matar processos"
            kill $TX_PID 2>/dev/null
            kill $RX_PID 2>/dev/null
            wait $TX_PID 2>/dev/null
            wait $RX_PID 2>/dev/null
            echo "$test_name,$fer,$tprop,$frame_size,0,0,$repeat" >> $CSV
            return
        fi
    done

    wait $TX_PID 2>/dev/null
    wait $RX_PID 2>/dev/null

    # extrair S do output do tx
    local s_tx=$(grep "Efficiency" /tmp/tx_output.txt | awk '{print $NF}')
    local time_tx=$(grep "Transfer time" /tmp/tx_output.txt | awk '{print $3}')

    if [ -z "$s_tx" ]; then
        s_tx="0"
    fi
    if [ -z "$time_tx" ]; then
        time_tx="0"
    fi

    echo "    S=$s_tx time=${time_tx}s"

    # verificar integridade
    if [ -f "$RX_FILE" ]; then
        if diff -q $TEST_FILE $RX_FILE > /dev/null 2>&1; then
            echo "    Ficheiro OK"
        else
            echo "    AVISO: ficheiro diferente!"
        fi
    else
        echo "    AVISO: ficheiro nao recebido"
    fi

    echo "$test_name,$fer,$tprop,$frame_size,$s_tx,$time_tx,$repeat" >> $CSV
}

echo ""
echo "=========================================="
echo " TESTE 1: Variar FER (tprop=0, frame=256)"
echo "=========================================="
for fer in 0.0 0.01 0.05 0.1 0.2 0.5; do
    for r in $(seq 1 $REPEATS); do
        run_test "vary_fer" $fer 0 256 $r
        sleep 1
    done
done

echo ""
echo "============================================="
echo " TESTE 2: Variar T_prop (FER=0, frame=256)"
echo "============================================="
for tprop in 0 10 50 100 200 500; do
    for r in $(seq 1 $REPEATS); do
        run_test "vary_tprop" 0.0 $tprop 256 $r
        sleep 1
    done
done

echo ""
echo "================================================"
echo " TESTE 3: Variar frame size (FER=0, tprop=0)"
echo "================================================"
for fsize in 64 128 256 512 1024; do
    for r in $(seq 1 $REPEATS); do
        run_test "vary_framesize" 0.0 0 $fsize $r
        sleep 1
    done
done

echo ""
echo "============================================="
echo " TESTE 4: FER x T_prop (frame=256)"
echo "============================================="
for fer in 0.05 0.1; do
    for tprop in 50 100; do
        for r in $(seq 1 $REPEATS); do
            run_test "fer_x_tprop" $fer $tprop 256 $r
            sleep 1
        done
    done
done

# limpar
rm -f $RX_FILE /tmp/tx_output.txt /tmp/rx_output.txt

echo ""
echo "=========================================="
echo " TESTES CONCLUIDOS"
echo " Resultados em: $CSV"
echo "=========================================="

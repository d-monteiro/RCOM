# download

Cliente FTP simples que descarrega um ficheiro a partir de uma URL.

## Compilar

    make

## Correr

    ./download ftp://[user:password@]host/path

Exemplos:

    ./download ftp://ftp.netlab.fe.up.pt/pub/RCOM/eot.txt
    ./download ftp://demo:password@test.rebex.net/readme.txt

Se nao forem dadas credenciais, usa `anonymous` / `anonymous@`.

# RCOM Lab 1 — Resumo Sem Palha

## O que é o projeto?

Transferir um ficheiro (ex: .gif) entre dois PCs ligados por cabo série RS-232, usando um protocolo **Stop-and-Wait** que tu vais implementar em **C no Linux**.

---

## Arquitetura — São 2 camadas

```
┌─────────────────────────┐
│   Application Layer     │  ← Lê o ficheiro, parte-o em bocados, empacota em packets
│   (tu programas isto)   │
├─────────────────────────┤
│   Data Link Layer       │  ← Manda frames pela porta série, garante que chegam bem
│   (tu programas isto)   │
├─────────────────────────┤
│   Porta Série (ttyS0)   │  ← O Linux trata disto, tu só usas a API (open/read/write/close)
└─────────────────────────┘
```

**Regra de ouro:** As camadas são independentes. O Data Link não sabe o que vai dentro dos packets. A Application não sabe como os frames funcionam. Só conhece a API: `llopen`, `llwrite`, `llread`, `llclose`.

---

## Parte 1 — Data Link Layer (DLL)

### O que faz?
- Abre e fecha a conexão entre os 2 PCs
- Empacota dados em **frames**
- Garante que os dados chegam sem erros (deteção + retransmissão)
- Usa Stop-and-Wait: manda 1 frame, espera confirmação, só depois manda o próximo

### Tipos de frames

**Frames de supervisão/controlo (S/U)** — sem dados, só sinalização:

| Frame | Código (C) | Para quê |
|-------|-----------|----------|
| SET   | 0x03      | Tx pede para abrir conexão |
| UA    | 0x07      | Confirmação de SET ou DISC |
| DISC  | 0x0B      | Pedir para fechar conexão |
| RR0   | 0x05      | Rx pronto para receber frame 0 |
| RR1   | 0x85      | Rx pronto para receber frame 1 |
| REJ0  | 0x01      | Rx rejeita frame 0 (erro nos dados) |
| REJ1  | 0x81      | Rx rejeita frame 1 (erro nos dados) |

Formato: `[FLAG] [A] [C] [BCC1] [FLAG]`

**Frames de informação (I)** — transportam dados:

Formato: `[FLAG] [A] [C] [BCC1] [D1...DN] [BCC2] [FLAG]`

| Campo | Valor |
|-------|-------|
| FLAG  | 0x7E (sempre) |
| A     | 0x03 (comandos do Tx / respostas do Rx) ou 0x01 (ao contrário) |
| C     | 0x00 = frame I nº 0, 0x40 = frame I nº 1 |
| BCC1  | A XOR C (protege o header) |
| BCC2  | D1 XOR D2 XOR ... XOR DN (protege os dados) |

### Numeração modulo-2
Alterna entre frame 0 e frame 1. Serve para detetar duplicados.

### Byte Stuffing (transparência)
O flag 0x7E pode aparecer dentro dos dados! Para resolver:
- Se 0x7E aparece nos dados → substitui por **0x7D 0x5E**
- Se 0x7D aparece nos dados → substitui por **0x7D 0x5D**
- O BCC2 é calculado ANTES do stuffing
- O destuffing é feito ANTES de verificar o BCC2

### Timer e Retransmissões (só no Transmitter)
```
enquanto retransmissões < máximo E sem resposta válida:
    envia o frame
    retransmissões++
    ativa timer
    enquanto timer ativo E sem resposta válida:
        tenta ler resposta (state machine)
        se resposta válida:
            desativa timer, continua
    se timer expirou:
        volta ao início do loop (reenvia)
se esgotou retransmissões:
    retorna erro
```

### Receção de frames — State Machine
Ler byte a byte da porta série com `read(fd, &buf, 1)`.

**Para frames S/U:**
```
Estado 0 → espera FLAG (0x7E)
Estado 1 → espera A (campo endereço)
Estado 2 → espera C (campo controlo)  
Estado 3 → espera BCC1, verifica A XOR C
Estado 4 → espera FLAG final
Estado 5 → FIM (frame válido)
```
Nota: podem vir vários FLAGs seguidos no início — lidar com isso.

**Para frames I:** igual até ao BCC1, depois continua a ler dados byte a byte até ao FLAG final. Fazer destuffing e verificar BCC2.

### Tratamento de erros no Receiver
- BCC1 errado → ignora o frame completamente
- BCC1 ok, frame é **duplicado** → descarta dados, envia RR
- BCC1 ok, frame é **novo**, BCC2 errado → descarta dados, envia REJ
- BCC1 ok, frame é **novo**, BCC2 ok → aceita dados, passa à App Layer, envia RR

---

## Parte 2 — Application Layer (AL)

### O que faz?
1. Envia um packet START com metadados do ficheiro (nome + tamanho)
2. Lê o ficheiro, parte em bocados, empacota cada bocado num packet DATA
3. Envia um packet END quando acabou

### Tipos de packets

**Packet de controlo (START / END):**
```
[C] [T1] [L1] [V1] [T2] [L2] [V2] ...
```
- C = 2 → START, C = 3 → END
- Os parâmetros vêm em triplos TLV:
  - T=0 → tamanho do ficheiro, L = nº de bytes, V = o valor
  - T=1 → nome do ficheiro, L = nº de bytes, V = o nome

**Packet de dados:**
```
[C] [L2] [L1] [P1] ... [Pk]
```
- C = 1 → é um packet de dados
- K = L2 × 256 + L1 (tamanho do payload)
- P1...Pk → fragmento do ficheiro

### Fluxo completo do Transmitter
```
1. llopen()                     → troca SET/UA com o Receiver
2. llwrite(START packet)        → envia metadados do ficheiro
3. para cada bocado do ficheiro:
     llwrite(DATA packet)       → envia fragmento
4. llwrite(END packet)          → sinaliza fim
5. llclose()                    → troca DISC/UA
```

### Fluxo completo do Receiver
```
1. llopen()                     → espera SET, responde UA
2. llread() → recebe START      → extrai nome e tamanho
3. loop: llread() → recebe DATA → escreve fragmento no ficheiro
4. llread() → recebe END        → sabe que acabou
5. llclose()                    → troca DISC/UA
```

---

## A API do Data Link Layer (o que ligas as 2 camadas)

| Função | O que faz |
|--------|-----------|
| `llopen(porta, TX/RX)` | Abre conexão. Tx envia SET e espera UA. Rx espera SET e responde UA. Retorna file descriptor. |
| `llwrite(fd, buffer, length)` | Empacota buffer num frame I, envia, espera RR/REJ, retransmite se preciso. Retorna bytes escritos. |
| `llread(fd, buffer)` | Espera frame I, desempacota, põe dados no buffer. Retorna nº de bytes lidos. |
| `llclose(fd)` | Fecha conexão com troca de DISC/UA. |

---

## Fases da comunicação (visão geral)

```
Tx                          Rx
│── SET ──────────────────→ │   ← Estabelecimento
│←──────────────────── UA ──│
│                           │
│── I (Ns=0) ─────────────→ │   ← Transferência
│←──────────────── RR (1) ──│
│── I (Ns=1) ─────────────→ │
│←──────────────── RR (0) ──│
│   ...                     │
│                           │
│── DISC ─────────────────→ │   ← Terminação
│←──────────────── DISC ────│
│── UA ───────────────────→ │
```

---

## Configuração da porta série — o essencial

```c
struct termios newtio;
newtio.c_cflag = B38400 | CS8 | CLOCAL | CREAD;  // 38400 baud, 8 bits, sem modem, ativa leitura
newtio.c_iflag = IGNPAR;     // Ignora erros de paridade
newtio.c_oflag = 0;          // Sem processamento de output
newtio.c_lflag = 0;          // Modo NÃO canónico (lê bytes crus, não linhas)
newtio.c_cc[VTIME] = 0;     // Sem timeout entre chars
newtio.c_cc[VMIN] = 0;      // read() não bloqueia (retorna 0 se não há dados)
```

Usar modo **não-canónico** e **VMIN=0** para ler byte a byte sem bloquear.

---

## Primeiros passos sugeridos

### Passo 1 — Porta série funcionar
- Abre a porta série (`/dev/ttyS0`)
- Configura com termios
- Testa enviar e receber bytes simples entre 2 PCs

### Passo 2 — State machine para frames S/U
- Implementa a máquina de estados para ler frames byte a byte
- Testa enviando SET e recebendo UA manualmente

### Passo 3 — llopen()
- No Tx: envia SET, espera UA (com timer)
- No Rx: espera SET, envia UA
- Quando os dois dizem OK, a conexão está aberta

### Passo 4 — llwrite() + llread() com frames I
- Implementa stuffing/destuffing
- Implementa BCC1 e BCC2
- Implementa o timer e retransmissões no Tx
- Implementa a lógica de RR/REJ no Rx

### Passo 5 — Application Layer
- Constrói e envia START packet com nome e tamanho
- Lê ficheiro em bocados, empacota em DATA packets, envia
- Envia END packet
- No Rx: reconstrói o ficheiro a partir dos packets recebidos

### Passo 6 — llclose()
- Tx envia DISC, Rx responde DISC, Tx responde UA

### Passo 7 — Testar com ficheiro real
- Transfere um .gif e verifica que o ficheiro recebido é idêntico ao original

---

## Avaliação — o que vão olhar

- Sincronização de frames funciona?
- Retransmissões funcionam?
- Resiste a erros?
- Packets de controlo (START/END) corretos?
- Ficheiro chega intacto?
- Código organizado com separação clara entre camadas?
- Relatório com análise de eficiência (S vs FER)
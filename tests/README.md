# Testes de Eficiencia do Protocolo

## Dependencias

```bash
pip install matplotlib
```

## Como correr

### 1. Compilar o projeto
```bash
cd ..
make clean && make
```

### 2. Iniciar o cable simulator
```bash
# Terminal 1
./bin/cable
```

### 3. Correr os testes
```bash
# Terminal 2
cd tests
bash run_tests.sh
```

Isto vai demorar uns minutos. Corre varias combinacoes de FER, T_prop e frame size,
repete cada teste 3 vezes, e guarda os resultados em `results.csv`.

### 4. Gerar graficos
```bash
python3 plot_results.py
```

Os graficos ficam em `tests/plots/`:
- `s_vs_fer.png` - Eficiencia vs Frame Error Rate
- `s_vs_a.png` - Eficiencia vs a (parametro de propagacao)
- `s_vs_framesize.png` - Eficiencia vs tamanho do frame
- `s_vs_fer_tprop.png` - Eficiencia vs FER para diferentes delays

## Parametros testados

| Teste | FER | T_prop (ms) | Frame Size |
|-------|-----|-------------|------------|
| Variar FER | 0, 0.01, 0.05, 0.1, 0.2, 0.5 | 0 | 256 |
| Variar T_prop | 0 | 0, 10, 50, 100, 200, 500 | 256 |
| Variar Frame Size | 0 | 0 | 64, 128, 256, 512, 1024 |
| Combinado | 0.05, 0.1 | 50, 100 | 256 |

## Formula teorica

Para Stop-and-Wait:
```
S = (1 - FER) / (1 + 2a)
```
onde `a = T_prop / T_frame` e `T_frame = frame_size * 8 / C`

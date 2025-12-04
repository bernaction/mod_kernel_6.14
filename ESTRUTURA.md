```
Lubuntu_RTOS/
│
├── 📄 README.md                    # Guia completo de instalação do kernel PREEMPT_RT
├── 📄 QUICKSTART.md                # 🚀 Guia rápido (COMECE AQUI!)
├── 📄 FULL_CLI.md                  # Comandos consolidados (copiar/colar)
├── 📄 TEST.md                      # Testes cyclictest/ptsematest
├── 📄 RESULTADOS.md                # Template para coletar resultados experimentais
├── 📄 .gitignore                   # Arquivos a ignorar no Git
│
├── 📁 pdf/
│   └── Trabalho M3 - 25-2-1.pdf    # Especificação do trabalho
│
├── 📁 src/
│   ├── 📄 main.c                   # Original ESP32 (referência)
│   ├── 📄 TRAB_M2.md               # Documentação do trabalho M2 (ESP32)
│   ├── 📄 README_LINUX.md          # 📖 Documentação técnica completa
│   ├── 📄 ADAPTACAO.md             # 🔄 Comparação ESP32 vs Linux
│   │
│   ├── 🔧 esteira_linux.c          # ✅ Esteira adaptada para Linux RT
│   ├── 🔧 servidor_periodico.c     # ✅ Servidor periódico (Parte 2)
│   └── 📄 Makefile                 # Compilação automática
│
└── 📄 periodic_server_example.c    # Exemplo do professor (referência)
```

---

## 📋 Fluxo de Trabalho Recomendado

### Parte 1: Instalação do Kernel RT

1. ✅ Baixar Lubuntu 24.04.3 LTS
2. ✅ Criar VM VirtualBox (60GB disco, 8GB RAM, 4 CPUs)
3. ✅ Seguir **README.md** ou **FULL_CLI.md** (comandos prontos)
4. ✅ Reiniciar e verificar kernel RT
5. ✅ Executar testes: `cyclictest`, `ptsematest` (ver **TEST.md**)

### Parte 2: Código Adaptado

6. ✅ Compilar: `cd src && make`
7. ✅ Executar esteira: `sudo ./esteira_linux`
8. ✅ Coletar métricas por 60s, anotar em **RESULTADOS.md**
9. ✅ Testar eventos: 'b' (OBJ), 'd' (E-STOP), 'h' (HMI)
10. ✅ Comparar com cyclictest rodando em paralelo

### Parte 3: Servidor Periódico

11. ✅ Executar: `sudo ./servidor_periodico 10 5 70 60`
12. ✅ Testar 3 configurações: alta/média/baixa utilização
13. ✅ Anotar: jobs executados, resposta máx, % idle

### Parte 4: Relatório

14. ✅ Comparar com ESP32 (ver **src/TRAB_M2.md**)
15. ✅ Capturas de tela
16. ✅ Análise de trade-offs (VM vs bare-metal)
17. ✅ Conclusões e limitações

---

## 🎯 Arquivos Essenciais por Fase

### Instalação
- **README.md** ou **FULL_CLI.md**
- **TEST.md** (para validação)

### Desenvolvimento
- **src/esteira_linux.c**
- **src/servidor_periodico.c**
- **src/Makefile**

### Documentação
- **QUICKSTART.md** (overview)
- **src/README_LINUX.md** (detalhes técnicos)
- **src/ADAPTACAO.md** (comparação ESP32↔Linux)

### Experimentos
- **RESULTADOS.md** (template vazio para preencher)
- **TEST.md** (referência de testes)

---

## 📊 Matriz de Leitura Recomendada

| Objetivo | Ler |
|----------|-----|
| Instalar kernel RT rápido | **FULL_CLI.md** |
| Entender cada passo | **README.md** |
| Executar programas | **QUICKSTART.md** |
| Detalhes técnicos | **src/README_LINUX.md** |
| Comparação ESP32 | **src/ADAPTACAO.md** |
| Coletar resultados | **RESULTADOS.md** |
| Validar RT | **TEST.md** |

---

## 🔍 Busca Rápida

**"Como compilar o kernel?"**  
→ README.md seção 1-7 **ou** FULL_CLI.md parte 1

**"Como executar a esteira?"**  
→ QUICKSTART.md seção "Execução"

**"Quais métricas coletar?"**  
→ RESULTADOS.md (todas as tabelas)

**"Diferenças ESP32 vs Linux?"**  
→ src/ADAPTACAO.md seção "Comparação de APIs"

**"Como funciona o servidor periódico?"**  
→ src/servidor_periodico.c (comentários no código)

**"Problemas de compilação?"**  
→ QUICKSTART.md seção "Troubleshooting"

---

## 🎓 Para Apresentação/Defesa

**Slides recomendados:**

1. **Instalação do Kernel RT**
   - Screenshot de `uname -r`
   - Screenshot de `cat /sys/kernel/realtime`
   - Gráfico de `cyclictest` (latências)

2. **Código Adaptado**
   - Tabela comparativa ESP32 vs Linux (src/ADAPTACAO.md)
   - Métricas da esteira (WCRT, (m,k)-firm)
   - Screenshot do terminal rodando

3. **Servidor Periódico**
   - Diagrama da fila + servidor
   - Gráfico: jobs executados × tempo
   - Análise de utilização

4. **Conclusões**
   - VM vs Bare-metal (latências)
   - Aplicabilidade do PREEMPT_RT
   - Limitações identificadas

---

## 💡 Dicas

- Use **FULL_CLI.md** para instalação rápida (copiar/colar blocos)
- Use **QUICKSTART.md** como checklist de tarefas
- Preencha **RESULTADOS.md** durante os testes (não depois!)
- Compare sempre com **src/TRAB_M2.md** (baseline ESP32)

---

## ✅ Checklist de Completude

- [ ] Kernel 6.14-rt3 instalado e verificado
- [ ] `/sys/kernel/realtime` == 1
- [ ] cyclictest executado (Max < 10 ms em VM)
- [ ] esteira_linux compilado e testado
- [ ] servidor_periodico compilado e testado
- [ ] RESULTADOS.md preenchido com métricas
- [ ] Capturas de tela coletadas
- [ ] Comparação ESP32 vs Linux feita
- [ ] Relatório final escrito

---

**Boa sorte no trabalho! 🚀**

# Makefile para Esteira Industrial - Linux RTOS
# Uso: make
#      sudo ./esteira_linux
#      sudo ./servidor_periodico

CC = gcc
CFLAGS = -Wall -Wextra -O2 -D_GNU_SOURCE
LDFLAGS = -pthread -lrt -lm

TARGET1 = esteira_linux
SOURCE1 = esteira_linux.c

TARGET2 = servidor_periodico
SOURCE2 = servidor_periodico.c

.PHONY: all clean run run-server

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(SOURCE1)
	$(CC) $(CFLAGS) -o $(TARGET1) $(SOURCE1) $(LDFLAGS)
	@echo "✅ $(TARGET1) compilado!"

$(TARGET2): $(SOURCE2)
	$(CC) $(CFLAGS) -o $(TARGET2) $(SOURCE2) $(LDFLAGS)
	@echo "✅ $(TARGET2) compilado!"
	@echo ""
	@echo "📌 Esteira:  sudo ./$(TARGET1)"
	@echo "📌 Servidor: sudo ./$(TARGET2) [Ts_ms] [Cs_ms] [prio] [duração_s]"
	@echo ""

clean:
	rm -f $(TARGET1) $(TARGET2)
	@echo "🧹 Limpeza concluída."

run: $(TARGET1)
	@echo "⚠️  Executando esteira com sudo (necessário para prioridades RT)..."
	sudo ./$(TARGET1)

run-server: $(TARGET2)
	@echo "⚠️  Executando servidor com sudo..."
	@echo "Usando: Ts=10ms Cs=5ms prio=70 duração=30s"
	sudo ./$(TARGET2) 10 5 70 30

help:
	@echo "Makefile - Esteira Industrial Linux RTOS"
	@echo ""
	@echo "Alvos disponíveis:"
	@echo "  make              - Compila todos os programas"
	@echo "  make clean        - Remove executáveis"
	@echo "  make run          - Compila e executa esteira_linux"
	@echo "  make run-server   - Compila e executa servidor_periodico"
	@echo "  make help         - Mostra esta ajuda"
	@echo ""
	@echo "Executáveis:"
	@echo "  esteira_linux     - Simulação da esteira industrial"
	@echo "  servidor_periodico - Teste de servidor periódico"
	@echo ""
	@echo "Comandos esteira_linux:"
	@echo "  b - Simula detecção de objeto (SORT_ACT)"
	@echo "  d - Aciona E-STOP (SAFETY)"
	@echo "  h - Aumenta setpoint via HMI"
	@echo "  q - Encerra programa"
	@echo ""
	@echo "Uso servidor_periodico:"
	@echo "  sudo ./servidor_periodico [Ts_ms] [Cs_ms] [prio] [duração_s]"
	@echo "  Exemplo: sudo ./servidor_periodico 10 5 70 60"

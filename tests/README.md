# Testes do FlowCount

O diretório `tests/` contém testes de host para os módulos principais do firmware. Eles compilam parte do código C real com adaptadores/fakes de ESP-IDF, permitindo validar regras de negócio sem uma placa conectada.

Voltar para o [README principal](../README.md) ou para a [documentação do firmware](../main/README.md).

## Objetivo

A suíte procura detectar regressões em:

- máquina de estados de contagem;
- fila e identidade dos eventos;
- relógio/SNTP;
- serialização MQTT;
- política de entrega;
- callbacks Wi-Fi/MQTT;
- buzzer e alertas;
- LEDs de contagem e conexão;
- perfis de build do firmware via CI.

Ela não substitui testes físicos de sensor, rede real, alimentação ou buzzer.

## Arquivos

```text
tests/
├── run_tests.sh
├── test_counter.c
├── test_production_event.c
├── test_communication.c
├── test_mqtt_protocol.c
├── test_network_alerts.c
├── test_buzzer.c
├── test_leds.c
├── test_app_time.c
├── fake_esp_idf.h
├── fake_network.h
├── fake_buzzer.h
├── fake_leds.h
├── fake_time.h
└── fake_time.c
```

## Dependências no Ubuntu/Debian

Instale:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libcjson-dev
```

A suíte usa por padrão o compilador disponível em `cc`. É possível selecionar outro por `CC`.

Exemplo:

```bash
CC=gcc bash tests/run_tests.sh
```

Para o teste do protocolo MQTT, o script procura cJSON em uma destas fontes:

1. `CJSON_DIR`, se definido;
2. componente `cJSON` do ESP-IDF ativo em `$IDF_PATH/components/json/cJSON`;
3. pacote de sistema descoberto por `pkg-config` como `libcjson`.

Se nenhuma fonte existir, o script termina com a mensagem solicitando `pkg-config`/`libcjson-dev`, ESP-IDF ativo ou `CJSON_DIR`.

## Executar todos os testes

Na raiz do projeto:

```bash
bash tests/run_tests.sh
```

O script usa `set -euo pipefail`, portanto a primeira falha encerra a execução com status diferente de zero.

## Sanitizers

Para executar AddressSanitizer e UndefinedBehaviorSanitizer:

```bash
SANITIZE=1 bash tests/run_tests.sh
```

O CI executa a suíte nos dois modos:

```text
SANITIZE=0
SANITIZE=1
```

## O que cada teste valida

### `test_counter.c`

Valida a máquina de estados pura de contagem.

Cenários principais:

- limiar exato de presença;
- limiar exato de liberação;
- dez episódios de peça parada/bloqueio;
- ruído e bordas intermediárias;
- boot com sensor já ocupado;
- 200 ciclos simulados a aproximadamente 30 peças/minuto;
- lacuna de amostragem e recuperação.

Saída esperada:

```text
OK: 6 cenarios (limiares, 10 bloqueios, ruido, partida, 200 ciclos, lacuna).
```

### `test_production_event.c`

Executa os módulos reais de contador, eventos e horário sobre uma implementação fake de fila/tarefas/RNG.

Valida:

- falha ao criar fila;
- restrição de tarefa proprietária;
- geração da sessão;
- FIFO de 72 eventos;
- 74 passagens com 2 overflows;
- sequência que continua após eventos perdidos;
- timestamps imutáveis;
- evento anterior ao SNTP permanecendo sem UTC;
- `peek`/`acknowledge`;
- estatísticas;
- mil ciclos adicionais de ocupação/liberação da fila;
- perfil de comunicação e perfil de diagnóstico.

O `run_tests.sh` compila esse teste com três combinações:

```text
COMM=0 / DIAG=0
COMM=0 / DIAG=1
COMM=1 / DIAG=0
```

### `test_app_time.c`

Valida:

- boot sem sincronização;
- SNTP chamado cedo demais;
- falha de inicialização do SNTP e retry;
- timeout de 60 s;
- captura UTC;
- imutabilidade de cópias já criadas;
- ressincronização com correção de horário;
- expiração da referência;
- rejeição de 1970;
- falha em `gettimeofday`;
- leitura de referência excessivamente lenta.

### `test_mqtt_protocol.c`

Utiliza cJSON real e valida:

- JSON de evento;
- timestamp ISO 8601 com fração;
- limites de buffers;
- evento sem UTC;
- parsing rigoroso de ACK;
- valores máximos de `uint32_t`/`uint64_t`;
- rejeição de payloads inválidos;
- validação de prefixo de tópico;
- helpers de retry/identidade de `delivery_t`.

Parte da infraestrutura de ACK testada aqui ainda não está conectada ao fluxo de entrega atual do firmware.

### `test_communication.c`

Valida diretamente `communication_process_once()` com filas simuladas:

- nenhum publish sem evento;
- um publish por evento;
- preservação da FIFO quando offline;
- preservação da cabeça quando o enqueue MQTT falha;
- envio do backlog após reconexão;
- ausência de reenvio pela fila da aplicação depois que um item já foi transferido ao outbox.

### `test_network_alerts.c`

Executa os managers reais de Wi-Fi e MQTT com APIs do ESP-IDF simuladas.

Valida:

- início da tentativa Wi-Fi;
- agendamento de reconexão;
- estado conectado ao receber IP;
- limpeza do estado em desconexão;
- início do cliente MQTT;
- notificação ao buzzer;
- bloqueio de publicação quando MQTT está desconectado;
- falha quando o outbox não aceita a mensagem.

### `test_buzzer.c`

Valida o sequenciador do buzzer:

- falhas em cada etapa de inicialização;
- inicialização idempotente;
- PWM em 2400 Hz;
- beep de 60 ms;
- intervalo entre beeps;
- alerta de três pulsos;
- agrupamento de queda Wi-Fi + MQTT;
- rearme depois da reconexão;
- 16 beeps pendentes e overflow;
- recuperação após falhas de LEDC.

### `test_leds.c`

Valida os LEDs com comunicação habilitada e desabilitada:

- verde apagado no boot e pulso imediato de 100 ms por produto;
- renovação da duração quando outra peça passa durante o pulso;
- contagem e pulso verde durante uma desconexão;
- vermelho contínuo até Wi-Fi e MQTT conectarem, incluindo quedas e reconexões;
- vermelho apagado quando a comunicação está desabilitada;
- nenhuma escrita GPIO quando o estado permanece igual;
- inicialização idempotente, falhas de configuração e de escrita dos dois LEDs;
- repetição de escritas que falharam sem bloquear o outro LED;
- rejeição de GPIO inválido, LEDs no mesmo GPIO e conflitos com buzzer, sensor e OLED.

## O que é fake e o que é real

Os testes compilam os arquivos de implementação reais do projeto, mas substituem APIs de plataforma por fakes.

Exemplos de elementos simulados:

- FreeRTOS Queue;
- Task handles;
- EventGroups;
- Wi-Fi;
- ESP-MQTT;
- LEDC;
- GPIO;
- `esp_timer`;
- SNTP;
- RNG.

Isso é adequado para regras determinísticas, mas não mede comportamento elétrico nem timing real do microcontrolador.

## Inconsistência conhecida nesta revisão

No snapshot analisado (`c64f2ff`), `communication.c` passou a descartar eventos sem UTC válido antes da publicação. Entretanto, `tests/test_communication.c` ainda cria seus eventos de teste preenchendo apenas `sequence`, deixando `clock_synced = 0` e `timestamp_ms = 0`.

Com o código atual, esses eventos entram no caminho de descarte e o teste que espera publicação falha. Para alinhar o teste ao comportamento atual, os eventos usados nos cenários de publicação precisam representar eventos válidos, por exemplo com:

```c
.clock_synced = 1,
.timestamp_ms = 1700000000000
```

Os cenários específicos para evento sem UTC devem, por sua vez, verificar explicitamente a política de descarte. Até esse teste ser atualizado, não trate uma falha em `test_communication.c` como evidência automática de regressão da conexão MQTT.

## Integração contínua

O workflow está em:

```text
.github/workflows/ci.yml
```

A CI executa em `ubuntu-24.04`.

### Job de testes de host

Matriz:

```text
sanitize=0
sanitize=1
```

Dependências instaladas:

```text
build-essential
pkg-config
libcjson-dev
```

### Job de firmware

Utiliza:

```text
espressif/idf:v5.5.5
```

E compila dois perfis para target ESP32-S3:

```text
communication:
  CONFIG_FLOWCOUNT_COMM_ENABLED=y
  CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER=n

diagnostic:
  CONFIG_FLOWCOUNT_COMM_ENABLED=n
  CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER=y
```

Um job final `CI required` exige que testes e builds terminem com sucesso.

## Teste físico ainda necessário

Mesmo com a CI verde, antes de uma entrega valide em bancada:

- E18-D80NK real;
- tempos de presença/liberação na esteira;
- 2N2222A e KC-1206;
- GPIO45 durante boot;
- reconexão Wi-Fi em roteador real;
- queda e retorno da Raspberry/Mosquitto;
- backlog de eventos;
- ingestão Mosquitto -> Telegraf -> InfluxDB -> Grafana;
- reboot do ESP32 com eventos pendentes;
- perda de energia da Raspberry.

## Adicionando novos testes

Ao criar um módulo com lógica independente, prefira manter a regra em código C testável sem depender diretamente de hardware. Depois:

1. crie `tests/test_<modulo>.c`;
2. adicione somente os fakes necessários;
3. compile com `-Wall -Wextra -Werror -pedantic`;
4. inclua a execução em `tests/run_tests.sh`;
5. confirme também `SANITIZE=1`;
6. deixe a CI compilar o firmware real para detectar incompatibilidades de ESP-IDF.

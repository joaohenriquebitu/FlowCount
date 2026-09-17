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

## Preparar o checkout

Execute os testes no computador de desenvolvimento, em Bash. Se ainda não clonou o projeto:

```bash
mkdir -p ~/projetos
cd ~/projetos
git clone https://github.com/joaohenriquebitu/FlowCount.git
cd FlowCount
git checkout --detach c7f9f8405eaf07489e9a738111b7240adb02af09
```

Se já possui o checkout, entre nele sem cloná-lo novamente e registre `git rev-parse HEAD`. Os testes de host não precisam de ESP32, Raspberry, Docker ou ESP-IDF quando cJSON está instalado no computador. Para compilar e gravar o firmware, siga o [guia de instalação](../docs/instalacao.md).

## Dependências no Ubuntu/Debian

Instale:

```bash
sudo apt update
sudo apt install -y git build-essential pkg-config libcjson-dev
```

Confira as ferramentas antes de executar:

```bash
cc --version
pkg-config --modversion libcjson
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

Se nenhuma fonte existir, o script termina com a mensagem solicitando `pkg-config`/`libcjson-dev`, ESP-IDF ativo ou `CJSON_DIR`. Para usar uma cópia do código-fonte cJSON, aponte `CJSON_DIR` para a pasta que contém **cJSON.c e cJSON.h**:

```bash
CJSON_DIR=/caminho/para/cJSON bash tests/run_tests.sh
```

A mesma variável pode ser usada na execução com sanitizers. Não basta ter apenas a biblioteca de runtime instalada; são necessários os headers ou o código-fonte.

## Executar todos os testes

Na raiz do projeto:

```bash
bash tests/run_tests.sh
```

O script usa `set -euo pipefail`, portanto a primeira falha encerra a execução com status diferente de zero. Confira imediatamente após a execução:

```bash
echo $?
```

**Aprovação:** código `0` e todas as etapas concluídas, incluindo a última linha:

```text
OK: comunicacao publica eventos validos; offline/falha preservam FIFO; evento sem UTC e descartado.
```

Algumas linhas `OK` antes de um erro não significam que toda a suíte passou. O script compila em uma pasta temporária e a remove ao terminar.

## Sanitizers

Para executar AddressSanitizer e UndefinedBehaviorSanitizer:

```bash
SANITIZE=1 \
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
bash tests/run_tests.sh
```

O CI executa a suíte nos dois modos:

```text
SANITIZE=0
SANITIZE=1
```

**Aprovação:** código de saída `0`, todas as etapas concluídas e nenhum diagnóstico ASan/UBSan. Execute em um terminal normal; LeakSanitizer não funciona sob `ptrace` (como certos depuradores e sandboxes). Não desative a detecção de vazamentos para registrar um resultado equivalente à CI.

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
- ausência de reenvio pela fila da aplicação depois que um item já foi transferido ao outbox;
- descarte de eventos sem UTC válido, sem bloquear o envio do próximo evento válido.

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

## Eventos sem horário sincronizado

O teste de comunicação já cria eventos válidos com `clock_synced = 1` e `timestamp_ms` preenchido, além de eventos sem sincronização para verificar o descarte. Uma falha nesse teste deve ser investigada; a antiga ressalva sobre timestamps ausentes nos dados de teste não se aplica a esta revisão.

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

### Reproduzir os dois builds localmente

Depois de instalar e ativar ESP-IDF 5.5.5 conforme o [guia de instalação](../docs/instalacao.md), execute na raiz do FlowCount. Os builds usam configurações temporárias e não sobrescrevem o `sdkconfig` com suas credenciais:

```bash
FLOWCOUNT_BUILD_ROOT="$(mktemp -d /tmp/flowcount-build-check.XXXXXX)"
printf '%s\n' 'CONFIG_FLOWCOUNT_COMM_ENABLED=y' 'CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER=n' > "$FLOWCOUNT_BUILD_ROOT/communication.defaults"
printf '%s\n' 'CONFIG_FLOWCOUNT_COMM_ENABLED=n' 'CONFIG_FLOWCOUNT_DIAGNOSTIC_CONSUMER=y' > "$FLOWCOUNT_BUILD_ROOT/diagnostic.defaults"
idf.py -B "$FLOWCOUNT_BUILD_ROOT/build-communication" -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG="$FLOWCOUNT_BUILD_ROOT/sdkconfig-communication" \
  -DSDKCONFIG_DEFAULTS="$FLOWCOUNT_BUILD_ROOT/communication.defaults" build
idf.py -B "$FLOWCOUNT_BUILD_ROOT/build-diagnostic" -DIDF_TARGET=esp32s3 \
  -DSDKCONFIG="$FLOWCOUNT_BUILD_ROOT/sdkconfig-diagnostic" \
  -DSDKCONFIG_DEFAULTS="$FLOWCOUNT_BUILD_ROOT/diagnostic.defaults" build
```

**Aprovação:** os dois perfis compilam sem erro. Esses builds não contêm credenciais de rede e servem para validação de compilação; para gravar a estação conectada, use a configuração do guia de instalação. Os artefatos permanecem na pasta temporária indicada por `FLOWCOUNT_BUILD_ROOT`.

## Diagnóstico dos testes automatizados

| Falha | Ação |
|---|---|
| `cc` não encontrado | Instalar `build-essential` ou selecionar um compilador disponível com `CC` |
| cJSON não encontrado | Instalar `libcjson-dev` e `pkg-config`, ou definir `CJSON_DIR` com os fontes |
| `Assertion failed` / erro ASan ou UBSan | Registrar a saída e o cenário; tratar como falha da suíte até investigar |
| LeakSanitizer informa `ptrace` | Reexecutar em terminal fora do depurador/sandbox, mantendo `detect_leaks=1` |
| `idf.py` não encontrado | Ativar ESP-IDF com `. ~/esp/esp-idf/export.sh` |
| Build falha antes de compilar | Conferir versão ESP-IDF, target `esp32s3` e dependências do guia de instalação |

## Verificação desta revisão documental

Em 16/09/2026, a suíte de host foi executada até o fim com o código do firmware na revisão `c7f9f8405eaf07489e9a738111b7240adb02af09`, usando cJSON 1.7.17 via `CJSON_DIR`. Os modos normal e `SANITIZE=1`, com detecção de vazamentos ativa, terminaram com código zero. A execução com sanitizers ocorreu fora do sandbox, pois LeakSanitizer não funciona sob sua instrumentação.

Os builds ESP32-S3 e os ensaios com Raspberry, servidor em execução e hardware físico não foram executados nessa revisão documental. Os passos do servidor foram conferidos contra seu Compose, fluxos Node-RED, schema SQL e provisionamento Grafana na revisão indicada no guia de instalação. O aceite completo depende dos ensaios abaixo.

## Ensaios de bancada e integração

Execute depois de concluir o [guia de instalação](../docs/instalacao.md). Use uma bancada exclusiva, como `TESTE-FISICO`, configurada em `FLOWCOUNT_BANCADA`; pare simuladores e outros publicadores com essa identificação. Os ensaios geram registros reais no banco.

Registre a revisão do firmware e do servidor, a bancada, o horário do ensaio e o resultado de cada etapa. Os critérios abaixo avaliam o protótipo; os testes de host não substituem estes ensaios.

### 1. Preparação

1. Confira alimentação, GND comum, interface do sensor e pinagem no [guia de hardware](../docs/hardware.md).
2. Grave o perfil de comunicação (`FLOWCOUNT_COMM_ENABLED=y`, `FLOWCOUNT_DIAGNOSTIC_CONSUMER=n`).
3. Deixe o sensor livre e abra o monitor serial.
4. Aguarde `Wi-Fi conectado`, `MQTT conectado` e `CLOCK_SYNCED`. O LED vermelho deve apagar quando Wi-Fi e MQTT estiverem conectados; isso, sozinho, não comprova sincronização do relógio.
5. Na Raspberry, dentro do repositório do servidor, consulte o total de referência:

```bash
docker compose exec -T postgres psql -U saap -d saap \
  -c "SELECT coalesce(sum(delta), 0) AS total FROM producao WHERE bancada = 'TESTE-FISICO';"
```

Anote esse valor como **N**. Repita a consulta após cada ensaio, aguardando alguns segundos para ingestão. Como ela não limita o período, a comparação não muda por causa da janela de tempo do painel.

### 2. Contagem controlada

Passe uma peça pelo sensor 10 vezes. Em cada passagem, mantenha presença por pelo menos 1 segundo e depois deixe o sensor livre por pelo menos 1 segundo.

**Aprovação:** 10 confirmações locais (pulso verde e aviso sonoro), total SQL **N + 10** e aumento correspondente no Grafana, com a bancada e o intervalo corretos. Uma peça parada não deve gerar contagens repetidas.

### 3. Peça parada

Anote um novo total **N**, bloqueie o sensor por 10 segundos e consulte o banco antes de liberar.

**Aprovação:** total permanece **N** durante o bloqueio; depois de liberar por 1 segundo e aguardar a ingestão, total passa a **N + 1**. Sem novas passagens, deve permanecer nesse valor.

### 4. Queda e retorno do broker

Faça este ensaio apenas no servidor de teste, pois interrompe todas as estações conectadas. Comece com relógio sincronizado e nenhuma publicação pendente. Anote um novo **N**.

Na Raspberry:

```bash
docker compose stop mosquitto
```

Aguarde a desconexão no monitor serial e o LED vermelho aceso. Sem reiniciar a placa, faça 5 passagens completas. O LED verde deve continuar sinalizando; o total no banco permanece **N**. Em seguida, restaure o broker:

```bash
docker compose start mosquitto
```

**Aprovação:** MQTT reconecta, o LED vermelho apaga e os cinco eventos pendentes chegam ao banco, totalizando **N + 5**. Espere até 60 segundos, observando os logs; se não concluir, investigue a conexão e o Node-RED. Finalize sempre com o broker ligado.

Esse ensaio usa apenas cinco eventos, abaixo da capacidade padrão de 72, e deve terminar antes da expiração do horário sincronizado. Ele não demonstra persistência da fila após desligamento: a fila do ESP32 fica em RAM.

### 5. Reconexão Wi-Fi

Com o broker ativo e o relógio sincronizado, interrompa temporariamente apenas o acesso Wi-Fi da estação de teste. Se isso afetar outros equipamentos, use um ponto de acesso dedicado.

**Aprovação:** log de desconexão e LED vermelho aceso; contagem local continua. Após restabelecer a rede, Wi-Fi e MQTT reconectam, o LED vermelho apaga e cinco passagens feitas durante a interrupção aumentam o total SQL em cinco. Não reinicie o ESP32 durante este ensaio.

### 6. Validação e deduplicação no servidor

Execute o [teste de publicação e reenvio](../docs/instalacao.md#17-validar-mqtt-gravação-e-deduplicação). **Aprovação:** publicar duas vezes o mesmo `(bancada, evt_id)` mantém uma única linha no banco.

Para testar rejeição, no mesmo terminal em que a variável `FLOWCOUNT_MQTT_PASSWORD` foi preenchida, publique um evento com timestamp inválido:

```bash
mosquitto_pub -h localhost -u flowcount -P "$FLOWCOUNT_MQTT_PASSWORD" \
  -q 1 -t fabrica/setorA/bancada/TESTE-INVALIDO/evento \
  -m '{"bancada":"TESTE-INVALIDO","evt_id":"teste-invalido","ts":"invalido","delta":1}'
```

**Aprovação:** o Debug do Node-RED indica evento inválido e a consulta abaixo retorna zero:

```bash
docker compose exec -T postgres psql -U saap -d saap \
  -c "SELECT count(*) FROM producao WHERE bancada = 'TESTE-INVALIDO' AND evt_id = 'teste-invalido';"
```

### 7. Persistência do servidor

Com o sensor parado e todos os eventos já gravados, anote **N** e reinicie a Raspberry com `sudo reboot`. Após reconectar por SSH, entre em `~/Grafana_Dashboards`, confira `docker compose ps` e consulte novamente o banco.

**Aprovação:** o total continua **N** e o histórico aparece no Grafana. Depois que a estação reconectar, uma nova passagem aumenta o total para **N + 1**.

### 8. Reinicialização da estação

Com todos os eventos já gravados, reinicie apenas o ESP32 e mantenha o sensor livre. Aguarde novamente `CLOCK_SYNCED` e conexão MQTT; faça uma passagem completa.

**Aprovação:** o histórico anterior permanece, a nova passagem acrescenta uma linha e seu `evt_id` pertence a uma nova sessão. Não espere recuperação de eventos que estavam apenas na RAM antes do reset. Eventos gerados sem UTC válido são descartados para publicação, mesmo que o relógio sincronize depois.

### Registro de aceite

| Ensaio | Evidência a guardar |
|---|---|
| Host normal e sanitizers | Código de saída zero e saída final da suíte |
| Build ESP32-S3 | Revisão, perfil compilado e build sem erro |
| Contagem / peça parada | Totais antes/depois e sinalização observada |
| Broker / Wi-Fi | Logs de queda/retorno e cinco eventos recuperados |
| Deduplicação / inválido | Contagens SQL de 1 e 0, respectivamente |
| Reboots | Histórico preservado e nova passagem registrada |

Se algum critério falhar, registre a falha e siga o [diagnóstico por camada](../docs/raspberry.md#diagnóstico-por-camada). Não considere o sistema aprovado apenas porque o dashboard abriu.


## Adicionando novos testes

Ao criar um módulo com lógica independente, prefira manter a regra em código C testável sem depender diretamente de hardware. Depois:

1. crie `tests/test_<modulo>.c`;
2. adicione somente os fakes necessários;
3. compile com `-Wall -Wextra -Werror -pedantic`;
4. inclua a execução em `tests/run_tests.sh`;
5. confirme também `SANITIZE=1`;
6. deixe a CI compilar o firmware real para detectar incompatibilidades de ESP-IDF.

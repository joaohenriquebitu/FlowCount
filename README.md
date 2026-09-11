# FlowCount

Protótipo de contagem de peças com Heltec WiFi LoRa 32 V3 / ESP32-S3 e sensor
fotoelétrico. **Etapa atual: 3, preparação do horário real dos eventos.**
A contagem e a fila das etapas anteriores foram preservadas. Cada evento guarda
UTC em milissegundos, qualidade temporal e tempo monotônico complementar.
O módulo SNTP está implementado para ativação após conectividade, mas **ainda não
há Wi-Fi nem sincronização real validada na placa**. Sem sincronização, UTC é
explicitamente inválido. Não há MQTT, backend, OLED, buzzer ou persistência em Flash.

## Organização do código

O firmware permanece no componente `main` do ESP-IDF. Os headers públicos ficam
em `include/` e as implementações em `src/`, com a mesma divisão por responsabilidade:

```text
main/
├── CMakeLists.txt
├── Kconfig.projbuild
├── include/
│   ├── communication/   # Comunicação, Wi-Fi, MQTT e protocolo
│   ├── counting/        # Contagem e eventos de produção
│   ├── indicators/      # Sinalização por buzzer
│   └── time/            # Relógio e sincronização SNTP
└── src/
    ├── main.c           # Ponto de entrada e integração da aplicação
    ├── communication/
    ├── counting/
    ├── indicators/
    └── time/
```

Os includes usam o caminho do módulo, por exemplo, `#include "counting/counter.h"`.
Ao adicionar um módulo, coloque o `.h` e o `.c` nas pastas correspondentes e
registre a implementação em `main/CMakeLists.txt`. Os testes de host e seus
adaptadores permanecem em `tests/`.

## Ambiente e compilação

Compilação verificada com **ESP-IDF v5.5.5**, target **esp32s3**. No computador
usado para esta alteração, o instalador EIM fornece a ativação abaixo:

```bash
cd /home/irlan-barros/projetos/FlowCount
source /home/irlan-barros/.espressif/tools/activate_idf_v5.5.5.sh
idf.py --version
idf.py -B build -DIDF_TARGET=esp32s3 build
```

Em outra instalação, ative o ambiente ESP-IDF 5.5.5 pelo script dessa instalação.
Se já existir uma configuração para outro chip, execute `idf.py set-target esp32s3`
antes do build (esse comando recria a configuração/build). As configurações locais
Windows em `.vscode` e a imagem `latest` do devcontainer não fixam um ambiente
reproduzível; a versão comprovada nesta etapa é a informada acima.

Com a placa conectada, identifique a porta em `ls /dev/serial/by-id/` ou
`ls /dev/ttyACM* /dev/ttyUSB*`. Substitua `/dev/ttyACM0` pela porta correta:

```bash
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

Saia do monitor com **Ctrl+]**. O flash e os testes físicos não foram executados
nesta revisão. Para salvar a sessão do ensaio, pode-se executar:

```bash
mkdir -p output/tests
idf.py -p /dev/ttyACM0 monitor 2>&1 | tee output/tests/etapa1-monitor.log
```

## Integração contínua (GitHub Actions)

O workflow `.github/workflows/ci.yml` executa em pushes de qualquer branch,
pull requests destinados à `main`, filas de merge e acionamento manual. Não há
filtro por arquivos: alterações de documentação também produzem o check exigido.

- **Host tests:** contador, relógio, fila e protocolo MQTT, com warnings tratados
  como erros. A suíte roda sem sanitizadores e com AddressSanitizer,
  UndefinedBehaviorSanitizer e detecção de vazamentos.
- **ESP32-S3 build:** compila com ESP-IDF **5.5.5**, nos perfis de comunicação e
  diagnóstico, usando configurações novas e sem credenciais. Verifica também os
  módulos dependentes do SDK que os testes de host não executam.
- **CI required:** só passa quando todas as variantes de testes e build passam;
  falhas, cancelamentos ou jobs ignorados não aprovam esse check.

### Exigir aprovação antes do merge

O workflow, sozinho, não bloqueia merges. Após publicar esta alteração e executar
a CI pela primeira vez, um administrador deve configurar a proteção da `main`
em **Settings → Branches → Branch protection rule** (ou regra equivalente em
**Rules → Rulesets**):

1. Exigir pull request antes do merge.
2. Ativar **Require status checks to pass before merging** e selecionar
   **CI required**, com origem GitHub Actions.
3. Exigir a branch atualizada com `main` antes do merge (ou usar fila de merge).
4. Aplicar a regra também aos administradores e não permitir bypass, se a
   exigência deve valer para todos.

Essa configuração é feita no GitHub; não é ativada pela presença deste arquivo.
Consulte a [documentação de proteção de branches](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-protected-branches/about-protected-branches).

### Executar a suíte completa localmente

No Ubuntu/Debian, instale `build-essential`, `pkg-config` e `libcjson-dev`.
Alternativamente, ative o ESP-IDF ou defina `CJSON_DIR` para a pasta que contém
`cJSON.c` e `cJSON.h`. Os testes usam o parser cJSON real.

```bash
bash tests/run_tests.sh
SANITIZE=1 ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 bash tests/run_tests.sh
```

É possível escolher o compilador com `CC=gcc` ou `CC=clang`. Em ambientes sob
`ptrace` que não suportam LeakSanitizer, use `ASAN_OPTIONS=detect_leaks=0` apenas
na execução local. A CI mantém a detecção habilitada. Uma dependência ausente ou
qualquer teste com falha encerra o script com erro, sem pular testes.

Os cenários MQTT verificam payload UTC, tamanhos de buffer, identidade da sessão,
ACKs malformados e fora dos limites, prefixos de tópico e prazos de reenvio. A
integração da fila roda com comunicação, sem comunicação e com diagnóstico,
verificando propriedade de acesso, retenção até ACK e estatísticas. Não simula
uma conexão real Wi-Fi/MQTT nem substitui os ensaios físicos.

## Ligação elétrica: confirmar antes do ensaio

- Entrada de contagem: **GPIO 7**, com **presença em LOW (0)** e repouso em HIGH (1).
- O firmware mantém pull-up e pull-down internos **desabilitados**, como antes.
- É necessário garantir eletricamente o nível de repouso. O código anterior
  mencionava resistor externo de **10 kΩ para 3,3 V**, mas sua instalação não foi
  comprovada. Confirmar a compatibilidade com a saída do sensor e registrar o
  valor efetivamente instalado; não presumir que o resistor já existe.
- Documentar alimentação e tipo de saída do sensor, referência de GND comum e
  adequação dos níveis ao GPIO de 3,3 V. Não aplicar diretamente ao GPIO a tensão
  de alimentação do sensor sem verificar o circuito de interface.
- Alimentar a placa por fonte conectada à tomada, conforme o escopo, sem bateria.
- A montagem real, a alimentação do sensor e os níveis medidos continuam pendentes
  de confirmação. Não deduzir a ligação apenas por cores de fios.

## Como a contagem funciona

`AGUARDAR LIVRE -> LIVRE -> CONFIRMAR PRESENÇA -> PRESENTE -> CONFIRMAR LIBERAÇÃO -> CONTAR / LIVRE`

1. Ao iniciar, exige liberação estável. Uma peça já presente no boot não é contada
   quando retirada; somente um ciclo iniciado depois de armar gera contagem.
2. LOW inicia confirmação de presença; retorno a HIGH antes de confirmar cancela
   essa tentativa. Toda atividade de borda reinicia o tempo de estabilidade.
3. Presença confirmada permanece armada enquanto a peça estiver diante do sensor.
4. HIGH inicia confirmação de liberação. Retorno a LOW cancela a liberação e
   preserva a mesma passagem. Nova estabilidade em HIGH é necessária.
5. Apenas a liberação confirmada incrementa o total, uma vez, e rearma a coleta.
6. Presença lógica prolongada emite um aviso por episódio. Uma liberação curta não
   resolve esse aviso. A liberação válida o encerra; se havia uma passagem armada,
   ela gera uma única contagem. O aviso não indica defeito nem parada de máquina.

A ISR nas duas bordas apenas marca uma flag de atividade protegida por spinlock.
A tarefa existente `app_main` lê GPIO e consome essa flag em seção crítica curta.
Não há logs, alocação nem contagem dentro da ISR. Não é necessário armazenar cada
borda: mesmo várias oscilações entre amostras invalidam a estabilidade. Esta flag
**não é a fila de eventos de produção**. A fila adicionada na Etapa 2 guarda
passagens completas, não bordas ou notificações de GPIO.

O módulo `main/src/counting/counter.c` contém a máquina de estados, sem dependência de ESP-IDF;
o módulo `main/src/main.c` cuida de GPIO, tempo monotônico, execução periódica e logs.
O estado da máquina e a geração das sequências são exclusivos de `app_main`.
A Etapa 2 acrescenta uma fila e, opcionalmente, uma tarefa de diagnóstico.
Falhas de configuração do GPIO/ISR usam `ESP_ERROR_CHECK`
e interrompem a inicialização com diagnóstico, em vez de seguir contando.

### Parâmetros iniciais (ajustáveis e ainda não calibrados)

| Parâmetro | Local | Valor inicial |
|---|---|---|
| Presença estável | `main/include/counting/counter.h`: `COUNTER_PRESENCE_US` | 30 ms |
| Liberação estável | `main/include/counting/counter.h`: `COUNTER_RELEASE_US` | 50 ms |
| Presença prolongada | `main/include/counting/counter.h`: `COUNTER_BLOCKED_US` | 5 s |
| Máxima lacuna de amostragem | `main/include/counting/counter.h`: `COUNTER_MAX_SAMPLE_GAP_US` | 100 ms |
| Período da tarefa | `main/src/main.c`: `SENSOR_SAMPLE_MS` | 10 ms, no mínimo 1 tick |

Os tempos são medidos com `esp_timer_get_time()` em microssegundos. O instante
de confirmação da passagem é preservado como tempo monotônico complementar;
o campo UTC separado depende da sincronização descrita na Etapa 3. O período é escalonado pelo FreeRTOS;
nenhum delay isolado decide se uma peça é válida. Observe o período em ticks no
log de inicialização ao mudar a frequência de tick do projeto.

Presença e intervalo livre precisam durar mais que suas confirmações, com margem
para a amostragem e escalonamento. Como ponto inicial de bancada, use LOW por pelo
menos **50 ms** e HIGH por pelo menos **70 ms**, com tarefa regular de 10 ms, e
meça esses tempos. Esses valores não são limites físicos já validados. A vazão de
30 peças/minuto (uma a cada 2 s) não informa sozinha a duração do pulso do sensor.

Se houver intervalo entre amostras maior que 100 ms, a máquina descarta o ciclo
incerto, registra **possível perda** e exige nova liberação estável. Isso evita
inventar contagem após uma lacuna; não recupera a passagem descartada. Qualquer
aviso desse tipo deve ser investigado antes de aprovar um ensaio. Pulsos menores
que o filtro, transições não capturadas pelo hardware e ruído que imita um ciclo
válido não podem ser distinguidos com garantia de uma passagem física; a montagem
elétrica e a calibração continuam necessárias.

### Interpretação do monitor

- `Coleta pronta: sensor livre.`: sistema armado para um novo ciclo.
- `Passagem valida/evento enfileirado: ... total=N ...`: ciclo confirmado,
  evento criado e copiado para a fila. Em lotação aparece `OVERFLOW evento perdido`,
  também com `total`, `sequence` e `perdidos`; a contagem continua.
- `Presenca prolongada: possivel sensor bloqueado.`: aviso após 5 s, uma vez por
  episódio; não produz incremento nem rearma a passagem.
- `Condicao de bloqueio encerrada: sensor liberado.`: liberação estável após aviso.
- `Lacuna de amostragem: ciclo descartado; possivel perda.`: ensaio precisa de
  investigação; aguardar novo log de coleta pronta para retomar.

## Testes automatizados da lógica

No diretório do projeto, com GCC instalado:

```bash
gcc -std=c11 -Wall -Wextra -Werror -pedantic -I main/include \
  main/src/counting/counter.c tests/test_counter.c -o /tmp/flowcount-test-counter
/tmp/flowcount-test-counter
```

O resultado esperado é `OK: 6 cenarios ...`, com saída de processo zero. Os testes
cobrem limites exatos dos tempos, dez episódios de peça parada, ruído na entrada e
saída (inclusive atividade entre amostras), partida ocupada, 200 ciclos simulados
na cadência de 30/min e recuperação após lacuna de observação. Uma falha de
asserção encerra o executável com erro.

A revisão também executou AddressSanitizer e UndefinedBehaviorSanitizer sem
achados. A detecção de vazamentos foi desativada porque LeakSanitizer não funciona
no ambiente sob ptrace; a lógica não usa alocação dinâmica. Os testes de software
não comprovam acurácia física nem o tempo de execução real da ISR/tarefa.

## Procedimento de bancada

1. **Preparar e registrar:** conferir ligação, fonte, repouso HIGH, sensor estável,
   peças separadas e trajetória unidirecional. Preencher a ficha abaixo, compilar,
   gravar e abrir o monitor. Esperar `Coleta pronta` antes de iniciar a medição.
2. **Passagem normal:** executar dez ciclos separados. Ao inserir a peça, o total
   não deve subir. Após retirar e confirmar liberação, deve subir exatamente um.
3. **Peça parada:** inserir uma peça e mantê-la por pelo menos 6 s. Esperar um aviso
   de bloqueio e nenhum incremento enquanto estiver presente. Retirar e verificar
   uma contagem e encerramento do aviso. Repetir dez vezes. Dar uma liberação breve
   inferior a 50 ms em um ensaio controlado: não deve contar nem resolver o aviso.
4. **Ruído controlado:** usando gerador/simulador compatível com a entrada de 3,3 V
   e com o sensor desconectado da saída do gerador, aplicar pulsos LOW de 10 ms
   separados por HIGH de 100 ms: zero contagens. Depois confirmar LOW por 100 ms,
   aplicar liberações HIGH de 10 a 20 ms intercaladas com LOW e finalizar com HIGH
   por pelo menos 70 ms: exatamente uma contagem ao final. Osciloscópio/analisador
   lógico ajuda a confirmar os tempos; movimento manual não prova pulsos de 10 ms.
5. **Sequência e acurácia:** executar 200 passagens observadas independentemente,
   registrando perdas e extras separadamente. Incluir fluxo de até 30/minuto, com
   duração medida de presença e liberação dentro das margens calibradas. Para um
   ensaio completo de 200 peças a 30/minuto, prever aproximadamente 6 min 40 s.
6. **Partida ocupada:** ligar/reiniciar com uma peça presente; após 5 s deve avisar
   bloqueio. Retirá-la não conta. Após `Coleta pronta`, o próximo ciclo conta uma.

Não use somente a diferença entre total observado e total esperado: uma perda e
uma contagem extra podem se anular. Correlacione as passagens individualmente com
os logs ou com vídeo/observação independente.

### Ficha mínima de ensaio

| Campo | Valor a registrar |
|---|---|
| Data, revisão do código e ESP-IDF | Pendente |
| GPIO, alimentação, interface e resistor externo | Pendente |
| Níveis medidos: livre / presença | Pendente |
| Posição, distância e fixação do sensor | Pendente |
| Velocidade e espaçamento entre peças | Pendente |
| Duração medida de LOW / HIGH | Pendente |
| Peças: material, cor, brilho e dimensões | Pendente |
| Iluminação e vibração | Pendente |
| Filtro de presença / liberação / bloqueio e tick | Pendente |
| Passagens observadas / perdas / extras | Pendente |
| Avisos de lacuna, logs e evidências | Pendente |

### Critérios para concluir a Etapa 1

- Build ESP32-S3 e testes automatizados passam.
- Pelo menos 200 passagens físicas, com **no máximo quatro erros somados** entre
  perdas e extras, nas condições registradas (meta de acurácia de 98%).
- Dez ensaios de peça parada sem contagens durante a presença; uma contagem por
  ciclo completo, com aviso de bloqueio e recuperação coerentes.
- Ensaios de ruído abaixo dos tempos de confirmação não geram contagens falsas.
- Fluxo até 30/min validado dentro da meta, sem lacunas de amostragem inexplicadas.
- Registro da montagem, calibração e evidências preenchido e reproduzível.

**Situação:** build e testes da lógica aprovados; critérios físicos pendentes.
A estrutura de eventos e fila já foi adicionada por solicitação explícita na
Etapa 2, descrita a seguir; isso não substitui a validação física da Etapa 1.

## Etapa 2: eventos e fila de contingência

### Modelo de dados e identidade

`main/include/counting/production_event.h` define o formato atual de **48 bytes**, ampliado dos
40 bytes da Etapa 2 para incluir UTC na Etapa 3. Não há ponteiros, strings longas
nem alocação por passagem:

```c
typedef struct {
    uint64_t sequence;
    int64_t occurred_at_us;
    int64_t timestamp_ms;
    uint8_t session[16];
    uint32_t station;
    uint32_t clock_synced;
} production_event_t;
```

- `station`: identidade centralizada em `menuconfig`, padrão 1; configurar um
  número distinto para cada estação.
- `session`: 128 bits aleatórios gerados no boot com `bootloader_random_enable()`,
  `esp_fill_random()` e `bootloader_random_disable()`, antes de qualquer ADC/RF.
  Unicidade probabilística, dependente da entropia, não garantia matemática. Não
  exige rede, relógio civil ou NVS. É exibida como 32 dígitos hexadecimais.
- `sequence`: 64 bits sem sinal, desde 1; também é o total da sessão. Somente
  passagens confirmadas criam eventos. O limite é verificado antes de incrementar.
- `occurred_at_us`: microssegundos monotônicos na confirmação da liberação,
  mantidos para diagnóstico e medição mesmo sem UTC.
- `timestamp_ms`: Unix UTC em milissegundos, com sinal e 64 bits; zero se inválido.
- `clock_synced`: 0 ou 1, indicando a qualidade do UTC na criação. Substitui o
  antigo `time_source`; são 32 bits para manter alinhamento explícito. Um `bool`
  não reduziria o tamanho total com o alinhamento atual.

A identidade permanece `(station, session, sequence)`. O timestamp não participa
da identidade. A sessão muda e a fila é perdida a cada reboot. Essa estrutura em
RAM não é formato de transporte; serialização será definida posteriormente.
Os campos temporais de cada evento são imutáveis após a criação. A estratégia
de UTC e as limitações antes da sincronização estão na seção da Etapa 3.

### Produtor, armazenamento e overflow

`app_main` chama `production_events_init()` antes de habilitar a coleta. Essa
inicialização cria a fila por `xQueueCreate()` e verifica falhas. A criação de cada
evento ocorre exclusivamente no ramo `COUNTER_COUNT`; não há alteração no módulo
`counter.c` ou na ISR da Etapa 1.

A fila copia `production_event_t` por valor. `xQueueSend(..., 0)` não espera espaço,
não sobrescreve eventos e tem o retorno verificado em todas as passagens. A fila
padrão tem **72 posições**, 20% de margem sobre 60, ou **144 segundos a 30/min**.
Sem consumidor, ela eventualmente lota mesmo sem existir qualquer falha de rede.

Quando cheia, a política é **descartar o novo evento**, mantendo os anteriores:

1. Criar o evento e atribuir uma nova identidade, aumentando o total local.
2. Tentar inserir sem espera.
3. Se falhar, incrementar `perdidos` e imprimir `OVERFLOW` com a identidade,
   timestamp, total e ocupação. A tarefa continua detectando peças.

É uma perda explícita do registro em RAM, não uma recuperação automática. Com
memória finita e sem consumidor, não é possível manter todos os eventos e contar
indefinidamente. Não se reutiliza a sequência rejeitada: se 73 e 74 forem perdidos,
o próximo evento será 75, mesmo que já exista espaço. Assim `total = eventos
aceitos desde o boot + perdidos`; a ocupação atual também depende do consumo.
`perdidos` mede apenas overflow da fila; lacunas de amostragem continuam com o
seu diagnóstico próprio na Etapa 1, sem inventar quantidade de peças perdidas.

O retorno `ESP_ERR_NO_MEM` de `production_events_record()` identifica esse overflow
esperado. `app_main` continua porque o módulo já registrou a perda. Erros de uso
(tarefa errada, tempo inválido ou sequência esgotada) interrompem a operação via
`ESP_ERROR_CHECK`, pois continuar comprometeria a identidade das passagens.

### Configuração e consumidor de diagnóstico

Execute `idf.py menuconfig` e abra **FlowCount - eventos de producao**:

| Opção | Padrão | Finalidade |
|---|---|---|
| `FLOWCOUNT_STATION_ID` | 1 | Identidade centralizada da estação |
| `FLOWCOUNT_EVENT_QUEUE_CAPACITY` | 72 | Capacidade da fila, mínimo 60 |
| `FLOWCOUNT_DIAGNOSTIC_CONSUMER` | Desabilitado | Remover e imprimir eventos para teste |
| `FLOWCOUNT_DIAGNOSTIC_DELAY_SECONDS` | 0 | Atraso inicial do consumidor quando habilitado |

As definições e padrões ficam em `main/Kconfig.projbuild`; seleções locais ficam em
`sdkconfig` (ignorado pelo Git). Registre essas seleções na ficha do ensaio. Refaça
build/flash após mudar opções; o reboot cria outra sessão e perde a fila anterior.

- **Sem consumidor (padrão):** os eventos permanecem na fila até lotar, desligar ou
  reiniciar. Nada os remove automaticamente.
- **Consumidor habilitado, atraso 0:** uma tarefa `event_diag` retira via
  `xQueueReceive()` e imprime todos os campos. Pode retirar antes do log do
  produtor; por isso `queue=0/72` após um envio bem-sucedido e ordem intercalada de
  logs são normais. O valor de ocupação é uma fotografia, não um contador acumulado.
- **Consumidor habilitado, atraso 180 s:** permite encher a fila e depois observar
  sua drenagem na mesma sessão, com timestamps e ordem originais.

O consumidor é **destrutivo e somente de diagnóstico**: remove imediatamente ao
receber, antes de imprimir, sem transmitir ou persistir. Um evento retirado só
existirá na cópia local da tarefa e eventualmente no log capturado; não há
confirmação de entrega. A política de remoção confiável para a comunicação futura
não foi implementada e não deve usar esse consumo de teste inadvertidamente.

### Concorrência, inicialização e memória

- A tarefa que inicializa é a única autorizada a produzir; o handle é conferido
  em cada chamada. Total/sequência e perdas são lidos/escritos apenas por ela.
- A ISR continua marcando somente a flag protegida por spinlock. Nenhuma API de
  produção é chamada em interrupção; não se usa `FromISR` para os eventos.
- O diagnóstico recebe cópias completas pela fila FreeRTOS e não lê os contadores
  de 64 bits do produtor. A sincronização da fila é responsabilidade do FreeRTOS.
- Não se mantém spinlock durante envio, logs ou esperas. Só o consumidor espera
  eventos; o produtor usa espera zero. Os logs seriais ainda têm custo de execução,
  a observar nos ensaios e no diagnóstico de lacunas da Etapa 1.
- Se a fila não puder ser criada, a coleta não começa. Se a tarefa opcional não
  puder ser criada, a fila é liberada e a inicialização falha com log claro.
- `sizeof(production_event_t) = 48`, confirmado por `_Static_assert` no build real.
  **48 × 72 = 3.456 bytes (3,375 KiB)** de dados, mais controle da fila e overhead
  do alocador. Há somente alocação inicial da fila, não por evento.
- Com diagnóstico habilitado, adicionar **4.096 bytes de stack** mais o controle
  da tarefa. Esse custo é pequeno para o ESP32-S3, mas deve ser reavaliado quando
  outros subsistemas forem integrados. Fila, total, sessão e perdas são voláteis.

### Testes automatizados da Etapa 2

No diretório do projeto:

```bash
bash tests/run_tests.sh
```

O script executa a regressão da Etapa 1 e duas variantes do módulo de eventos
(consumidor habilitado/desabilitado), com `-Wall -Wextra -Werror -pedantic`.
Os testes de eventos usam um adaptador de fila/tarefas/RNG somente no host; esse
adaptador não entra no firmware. Exercitam o código real de eventos integrado à
máquina de estados: ruído, peça parada, FIFO de 72 cópias, dois overflows sem
sobrescrita, preservação de tempos/identidade, retomada na sequência 75, mil ciclos
de produção/consumo e rejeição de produtor errado. Injetam falhas de criação da
fila e tarefa e verificam a liberação dos recursos e a desativação da entropia.

Opcionalmente, em ambiente compatível com sanitizers:

```bash
ASAN_OPTIONS=detect_leaks=0 SANITIZE=1 bash tests/run_tests.sh
```

A opção `detect_leaks=0` contorna a incompatibilidade do LeakSanitizer com o ptrace
neste ambiente; AddressSanitizer e UndefinedBehaviorSanitizer permanecem ativos.
Esses testes não simulam o escalonamento real, não validam a qualidade do RNG
físico nem substituem os testes com a fila FreeRTOS na placa.

### Execução e testes de bancada A-E

```bash
cd /home/irlan-barros/projetos/FlowCount
source /home/irlan-barros/.espressif/tools/activate_idf_v5.5.5.sh
idf.py menuconfig
idf.py -B build -DIDF_TARGET=esp32s3 build
idf.py -p /dev/ttyACM0 flash
idf.py -p /dev/ttyACM0 monitor
```

Substitua a porta pela real. Confirme a ligação descrita acima e espere `Coleta
pronta` antes do primeiro ciclo. O log inicial mostra estação, sessão, tamanho e
capacidade. Salve logs e seleções de configuração. A sequência reinicia a cada boot.

**A - Passagem única:** use consumidor desabilitado e fila vazia. Execute um ciclo
completo. Espere um único log `evento enfileirado`, `sequence=1`, `total=1`,
`queue=1/72`, `perdidos=0`, `mono_us` positivo. No firmware atual sem rede, esperar `timestamp_ms=0` e
`clock_synced=0`; isso não é falha da contagem. A inserção da
peça sozinha não deve gerar evento; a liberação confirmada gera exatamente um.

**B - Peça parada:** mantenha a peça por pelo menos seis segundos. Espere aviso
de bloqueio e nenhuma sequência adicional. Ao liberar corretamente, espere apenas
um evento. Repetir dez vezes e verificar também a recuperação do bloqueio.

**C - Passagens sucessivas:** execute dez ciclos separados. Confira sequências
consecutivas, mesma estação/sessão e `mono_us` crescente. UTC só é válido com
`clock_synced=1`. Depois habilite o
consumidor com atraso zero, recompile e grave: em uma nova sessão, cada identidade
aceita deve aparecer uma vez em `DIAG removido (nao persistido)`, com timestamp
idêntico ao do produtor. Registre a ordem por sequência, não pela ordem das linhas
intercaladas das tarefas. Não deve haver overflow nesse ensaio controlado.

**D - Capacidade/overflow:** consumidor desabilitado, nova sessão, gerar 74 ciclos.
Os primeiros 72 ocupam 1/72 até 72/72. O 73º e 74º devem imprimir `OVERFLOW`,
`total=73`/`74`, `perdidos=1`/`2`, mantendo 72 registros. Continuar passando peças
não pode causar reset nem sobrescrita silenciosa.

Para verificar recuperação sem reiniciar, faça outro ensaio com consumidor
habilitado e atraso inicial 180 s: produza 74 ciclos em aproximadamente 148 s a
30/min, iniciando logo após armar. Pause as peças até drenar. Confira que o
consumidor retira somente 1 a 72, com tempos originais; 73 e 74 foram explicitamente
perdidos. Passe outra peça após a drenagem: deve produzir 75, com `perdidos=2`.
Se o tempo de preparação impedir encher a fila, aumente o atraso configurado e
registre o valor. Não reinicie entre enchimento e drenagem.

**E - Memória/estabilidade:** consumidor habilitado, atraso zero, realizar pelo
menos 200 ciclos a até 30/min (aproximadamente 6 min 40 s). Conferir 200 eventos e
200 consumos únicos, `perdidos=0`, sem watchdog, resets ou lacunas inexplicadas.
Repetir com consumidor desabilitado por mais de 72 ciclos: devem existir somente
os overflows explícitos previstos. Uma sessão inesperadamente nova no meio do
ensaio evidencia reinício e invalida a continuidade do teste. Registrar qualquer
anomalia, logs completos, parâmetros e montagem; a observação no host não comprova
estabilidade de memória ou tempo real na placa.

### Situação e limite da entrega

A Etapa 2 implementa somente `detecção -> evento -> fila RAM`, com diagnóstico
opcional. Os testes automatizados passaram; a validação física A-E permanece
pendente. Os números de acurácia da Etapa 1 continuam dependentes da bancada.
O módulo temporal da Etapa 3 foi acrescentado conforme a seção abaixo. A
sincronização pela rede continua dependente da futura conectividade.

## Etapa 3: UTC dos eventos e sincronização SNTP

### Decisão de escopo e integração futura

Não existia Wi-Fi no repositório. Foi implementado `main/src/time/app_time.c` com a API
`esp_netif_sntp_init()` do **ESP-IDF 5.5.5**, sem adicionar camada de conexão ou
credenciais. `app_time_init()` inicializa somente o estado local e permite coletar
imediatamente. **No firmware atual o SNTP ainda não inicia**, pois não há rede.
Configurar o servidor em menuconfig não conecta a placa por si só.

Na Etapa 4, o responsável pela rede deverá inicializar ESP-NETIF e obter um IP.
Então, em contexto de tarefa, deverá chamar `app_time_start_sntp()` e verificar o
retorno, mantendo a coleta ativa mesmo se essa chamada falhar. A inicialização é
idempotente após sucesso, e uma falha permite nova tentativa pelo gestor de rede.
Não chamar essa função antes de inicializar a pilha de rede, nem dentro de ISR.
O módulo deve ser o único proprietário do cliente SNTP do SDK.

A configuração é centralizada em **FlowCount - horario dos eventos**:

| Opção | Padrão | Significado |
|---|---|---|
| `FLOWCOUNT_NTP_SERVER` | `pool.ntp.org` | Nome/IP do servidor; pode ser o NTP da Raspberry Pi |
| `FLOWCOUNT_CLOCK_MAX_AGE_SECONDS` | 86400 (24 h) | Validade máxima da última referência aceita |

A resincronização é a normal do lwIP/SNTP, configurada por
`CONFIG_LWIP_SNTP_UPDATE_DELAY`, atualmente **3.600.000 ms (1 h)**. Não há loop de
consultas manual. A validade máxima deve ser maior que esse intervalo. Servidor
vazio ou combinação inválida de tempos gera diagnóstico e mantém a coleta sem UTC.
A indisponibilidade de NTP nunca provoca espera da contagem nem reinício deliberado.

### Como o instante da ocorrência recebe UTC

1. A máquina de estados mantém os filtros baseados exclusivamente em
   `esp_timer_get_time()`.
2. O instante monotônico passado a `counter_update()` é mantido quando a função
   confirma a liberação. O evento é criado nessa mesma iteração, antes dos logs.
3. O callback de sincronização SNTP lê `gettimeofday()` e forma uma referência
   pareada UTC/monotônico. Apenas esse callback pode habilitar o estado sincronizado;
   uma data plausível no relógio de sistema, sozinha, não habilita UTC.
4. A referência é copiada sob seção crítica curta. O módulo calcula o UTC da
   ocorrência a partir da diferença monotônica desde essa referência e armazena
   o valor no evento antes de `xQueueSend()`.
5. O consumidor somente lê os campos já armazenados. Nunca consulta o relógio
   para substituir ou reconstruir o timestamp de um evento existente.

Isso relaciona o UTC ao instante confirmado da passagem, não à hora dos logs ou
consumo. Como na Etapa 1, o instante inclui a confirmação de liberação (50 ms por
padrão) e a amostragem; não representa exatamente a primeira borda elétrica.

`timestamp_ms` é um inteiro de 64 bits em **milissegundos desde a época Unix UTC**.
Oito bytes oferecem ampla capacidade para datas futuras e resolução mais útil
que segundos para eventos próximos. A resolução de 1 ms não significa exatidão
física de 1 ms: há incerteza de rede, amostragem, pareamento e deriva do oscilador.
O pareamento usa o ponto médio das leituras monotônicas antes/depois de
`gettimeofday()`; uma janela maior que 10 ms é rejeitada como referência imprecisa.
Datas anteriores a 2020, campos inválidos e falha de leitura também são rejeitados.

O estado é consultável por `app_time_is_synchronized()`. Os logs de sincronização
usam `gmtime_r()` e sufixo `Z`; não é necessário definir TZ para armazenar ou exibir
UTC. A conversão para horário brasileiro ficará na futura apresentação dos dados.

### Antes de sincronizar, após expirar e ao resincronizar

Sem referência SNTP aceita, cada passagem continua gerando identidade, sequência
e evento. São gravados `timestamp_ms=0`, `clock_synced=0` e `occurred_at_us` válido.
**Zero é sentinela de UTC inválido, não uma produção em 1970.** Os eventos antigos
permanecem assim mesmo quando o relógio sincroniza depois. Não há reconstrução
retroativa, descarte por falta de horário ou alteração da identidade.

Após sincronizar, novos eventos terão `clock_synced=1`. Sem novas respostas SNTP,
a referência continua sendo extrapolada pelo relógio monotônico por até 24 h;
esse período admite deriva e é uma política inicial ajustável, não uma exatidão
já medida. Após a validade configurada, novas capturas retornam UTC inválido.
Os eventos já criados mantêm a qualidade e o timestamp registrados originalmente.

A sincronização usa ajuste imediato (`smooth_sync=false`). Uma correção do horário
pode fazer o UTC de novos eventos avançar ou retroceder; a sequência continua
crescendo e é a referência de ordenação da sessão. Não se força monotonicidade do
UTC criando horários artificiais. Se uma atualização ocorrer entre a confirmação
e a captura e a ocorrência ficar anterior à referência recém-aceita, o módulo
marca esse evento conservadoramente como não sincronizado. Não tenta reconstruí-lo.

No reboot, o estado temporal começa novamente não sincronizado, mesmo que o RTC
mantenha uma data plausível. A sessão muda e a fila em RAM é perdida como antes.

### Concorrência, erros e logs

A tarefa produtora consulta o relógio, enquanto o callback do SNTP atualiza a
referência no contexto de rede. UTC, monotônico e validade são copiados juntos sob
`portMUX_TYPE`; isso também protege os campos de 64 bits no ESP32-S3. Não se usa
apenas `volatile`. Leituras de sistema, inicialização SNTP, formatação e logs ficam
fora da seção crítica. A ISR do sensor permanece inalterada.

`app_time_poll()` é chamado exclusivamente por `app_main`, limita suas verificações
a uma vez por segundo e emite diagnósticos de transição. Ele não espera horário
nem realiza consultas de rede. A inicialização SNTP usa `wait_for_sync=false`.
Falha de inicialização retorna erro com log; ausência de resposta por 60 s gera um
aviso único até recuperação. Uma referência inválida ou vencida degrada o UTC,
sem bloquear os eventos ou apagar dados da fila.

Logs esperados:

```text
CLOCK_UNSYNCED: aguardando conectividade; UTC invalido ate SNTP
... mono_us=... timestamp_ms=0 clock_synced=0 ...
```

Somente após a futura ativação com rede:

```text
SNTP iniciado: servidor=... intervalo=3600000 ms; sem espera da coleta
CLOCK_SYNCED UTC=...Z unix_ms=...
... mono_us=... timestamp_ms=... clock_synced=1 ...
```

### Memória e verificação automatizada

| Medida | Etapa 2 anterior | Etapa 3 atual | Aumento |
|---|---:|---:|---:|
| `sizeof(production_event_t)` | 40 bytes | 48 bytes | 8 bytes |
| Dados de 72 eventos | 2.880 bytes | 3.456 bytes | 576 bytes |

A fila mantém a capacidade e política de overflow. O módulo acrescenta também
algumas dezenas de bytes de estado estático. Os recursos da pilha de rede/SNTP,
quando ativados, são adicionais e não estão incluídos nessa conta do payload.
Nenhuma nova tarefa de relógio da aplicação foi criada. O consumidor opcional
continua com stack de 4.096 bytes mais seu controle.

Execute `bash tests/run_tests.sh`. Além das regressões, os testes agora exercitam
o módulo real de relógio com APIs de rede/tempo simuladas: boot, data plausível sem
sincronização, falha/repetição de inicialização SNTP, aviso de demora, sincronização,
quantização em ms, expiração e recuperação, correção para trás, referência inválida
e janela de leitura excessiva. Os testes integrados guardam eventos sem/com UTC,
simulam 30 segundos de espera e uma resincronização antes do consumo: todos os
campos originais permanecem intactos. O adaptador de testes não entra no firmware.
Esses resultados não validam rede, servidor NTP, latência ou precisão do hardware.

### Roteiro de bancada A-F

Ative o ambiente e compile conforme os comandos do início do README. Identifique
a porta com `ls /dev/serial/by-id/` ou a ferramenta do sistema; não suponha que ela
seja `/dev/ttyACM0`. Use `idf.py -p PORTA_REAL flash monitor`, substituindo o nome.
**Nenhuma porta serial estava disponível durante esta implementação.**

| Teste | Como executar e interpretar |
|---|---|
| **A - Boot sem sincronização** | Iniciar e passar peças. Esperar `CLOCK_UNSYNCED`, sequência crescente, UTC zero/flag 0 e tempo monotônico crescente. Nenhum bloqueio da coleta. Executável agora. |
| **B - Sincronização normal** | Após integração da rede, obter IP e chamar `app_time_start_sntp()`. Esperar início e `CLOCK_SYNCED`; comparar UTC com fonte de horário confiável. Depende da Etapa 4. |
| **C - Evento sincronizado** | Após B, passar uma peça. Esperar `clock_synced=1`; converter o Unix ms para UTC e comparar com o instante observado da liberação confirmada. Registrar diferença, sem alegar precisão não medida. |
| **D - Vários eventos** | Conferir sequências e `mono_us` crescentes; com relógio estável, UTC acompanha as passagens. Correções SNTP podem alterar a ordem UTC de novos eventos, sem alterar a sequência ou os antigos. |
| **E - Consumo atrasado** | Habilitar diagnóstico com atraso, por exemplo 60 s. Criar evento cedo e observar sua retirada pelo menos 30 s depois, na mesma sessão. Comparar `mono_us`, `timestamp_ms` e `clock_synced` por identidade: devem ser idênticos. Hoje valida também UTC inválido; repetir com UTC real após B. |
| **F - Reboot** | Reiniciar: nova sessão, sequência reiniciada, UTC inválido até nova sincronização. Executável agora para o estado inicial; repetir obtenção do UTC após B. |

Para verificar expiração, após haver rede funcional configure uma validade maior
que o intervalo SNTP, sincronize, bloqueie o acesso ao servidor e aguarde o prazo.
Novos eventos devem retornar à flag 0; antigos não mudam. Em ensaio acelerado,
reduza também o intervalo SNTP respeitando os limites do SDK e restaure os padrões
ao terminar. Não habilite um consumidor destrutivo se precisar manter os registros
na RAM; ele serve somente à validação e não comprova entrega a servidor.

### Status da Etapa 3

**Parcialmente concluída.** Modelo temporal, captura antes da fila, marcação de
UTC inválido, referência sincronizada thread-safe, SNTP preparado e testes estão
implementados. A sincronização real e os testes B/C (e partes sincronizadas de D-F)
dependem da futura conectividade e da bancada. Não foi implementado Wi-Fi, MQTT ou
backend, e não foi afirmado que o horário real já foi validado no ESP32-S3.

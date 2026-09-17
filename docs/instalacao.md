# Guia de instalação do FlowCount

[Voltar à apresentação do projeto](../README.md)

Para reproduzir o FlowCount completo, prepare primeiro o **servidor** e depois o **firmware da estação**. Dessa forma, o endereço do broker já estará definido quando o ESP32 for configurado.

As instruções abaixo consideram Ubuntu/Debian no computador de desenvolvimento e Raspberry Pi OS Lite 64-bit no servidor.

## 1. Preparar o servidor

Execute esta seção **na Raspberry Pi**, por SSH. Ela descreve uma instalação nova. Em uma instalação existente, faça backup dos dados e fluxos antes de atualizar: os volumes não são reinicializados por `docker compose up`.

### 1.1 Sistema operacional e rede

1. Grave **Raspberry Pi OS Lite (64-bit)** com o Raspberry Pi Imager.
2. Configure usuário, senha, SSH, país da rede sem fio e Wi-Fi, se não usar Ethernet.
3. Ligue a Raspberry e consulte seu IP no roteador. Reserve esse endereço por DHCP para que o firmware continue encontrando o broker.
4. Conecte pelo computador, substituindo usuário e IP:

```bash
ssh <usuario>@<ip-da-raspberry>
```

Na Raspberry:

```bash
sudo apt update
sudo apt full-upgrade -y
sudo apt install -y git curl nano mosquitto-clients python3 python3-venv
sudo reboot
```

Reconecte por SSH e confira rede e relógio:

```bash
ip -br address
timedatectl
```

O relógio deve estar correto e sincronizado. Anote o IPv4 da rede local. A Raspberry e a estação precisam se alcançar pela porta TCP 1883; o computador também acessará as portas 1880 e 3000. Use a rede local de teste: o editor Node-RED dessa configuração não exige login.

### 1.2 Docker

```bash
curl -fsSL https://get.docker.com -o /tmp/flowcount-install-docker.sh
sudo sh /tmp/flowcount-install-docker.sh
sudo usermod -aG docker "$USER"
exit
```

Entre novamente por SSH para aplicar o grupo e valide:

```bash
docker run --rm hello-world
docker compose version
```

O primeiro comando deve terminar com a mensagem de sucesso do Docker; o segundo deve informar uma versão do Compose.

### 1.3 Obter uma revisão conhecida do servidor

Os comandos foram conferidos no [servidor, revisão `6359597`](https://github.com/ValdimiroAlves/Grafana_Dashboards/tree/6359597918faf1b41a83b39492e5ba1cee65c281). Para reproduzir essa configuração:

```bash
cd ~
git clone https://github.com/ValdimiroAlves/Grafana_Dashboards.git
cd Grafana_Dashboards
git checkout --detach 6359597918faf1b41a83b39492e5ba1cee65c281
cp .env.example .env
nano .env
```

Defina **GRAFANA_PASSWORD** e **POSTGRES_PASSWORD**, substituindo os valores de exemplo. Para seguir a configuração de referência, evite `$` na senha do PostgreSQL. Guarde também a senha MQTT criada no próximo passo; são credenciais distintas.

### 1.4 Criar o usuário MQTT antes de iniciar o broker

O Mosquitto exige autenticação (`allow_anonymous false`). Ele não inicia sem `mosquitto/config/passwd`.

Na primeira instalação, crie o usuário `flowcount` com entrada interativa de senha:

```bash
docker run --rm -it \
  -v "$PWD/mosquitto/config:/mosquitto/config" \
  eclipse-mosquitto:2 \
  mosquitto_passwd -c /mosquitto/config/passwd flowcount
sudo chmod -R a+rX grafana mosquitto postgres
```

Use `-c` apenas para criar um arquivo novo; para atualizar um usuário em um arquivo existente, remova essa opção. A mesma credencial MQTT será usada no Node-RED, no firmware e nos testes de publicação.

### 1.5 Construir e iniciar os serviços

Ainda em `~/Grafana_Dashboards`:

```bash
docker compose config --quiet
docker compose up -d --build
docker compose ps
docker compose exec -T postgres pg_isready -U saap -d saap
```

O Compose deve listar `mosquitto`, `postgres`, `node-red` e `grafana` em execução, sem reinicializações contínuas. `pg_isready` deve retornar `accepting connections`; aguarde a inicialização e repita se necessário.

A imagem Node-RED instala o módulo PostgreSQL e inclui `flows.json` e `settings.js`. Na primeira criação, o volume `node_red_data` recebe esses arquivos automaticamente. O PostgreSQL executa os arquivos de `postgres/init/` apenas quando seu volume está vazio.

### 1.6 Configurar as credenciais do Node-RED

No navegador do computador, abra `http://<ip-da-raspberry>:1880`:

1. Localize o nó **eventos de producao** no fluxo de produção e abra-o com duplo clique.
2. Edite a configuração **Mosquitto local** pelo lápis. Mantenha host `mosquitto`, porta `1883` e tópico `fabrica/+/bancada/+/evento` no nó de entrada.
3. Na aba **Security**, informe usuário `flowcount` e a senha MQTT. Salve com **Update/Done**.
4. Abra **Gravar evento** e edite a configuração PostgreSQL (`saap@postgres:5432/saap`). Use host `postgres`, porta `5432`, banco `saap`, usuário `saap` e a senha **POSTGRES_PASSWORD** definida no `.env`.
5. Salve e clique em **Deploy**. A conexão MQTT deve aparecer como conectada.

Os nomes `mosquitto` e `postgres` são endereços internos do Docker; não use `localhost` nesses campos. O Node-RED **não recebe automaticamente** a senha PostgreSQL do `.env`. As credenciais salvas pelo editor persistem no volume `node_red_data`.

Confira erros de autenticação ou conexão:

```bash
docker compose logs --tail=100 mosquitto node-red postgres
```

### 1.7 Validar MQTT, gravação e deduplicação

Execute na Raspberry, em Bash, dentro de `~/Grafana_Dashboards`. O teste gera dados identificados como `TESTE-MANUAL`; eles também podem aparecer nos totais gerais do painel.

```bash
read -r -s -p 'Senha MQTT de flowcount: ' FLOWCOUNT_MQTT_PASSWORD
printf '\n'
FLOWCOUNT_TEST_ID="manual-$(date -u +%Y%m%dT%H%M%S)-$$"
FLOWCOUNT_TEST_TS="$(date -u +%Y-%m-%dT%H:%M:%S.%3NZ)"
FLOWCOUNT_TEST_PAYLOAD="$(printf '{"bancada":"TESTE-MANUAL","evt_id":"%s","ts":"%s","delta":1,"origem":"teste-manual"}' "$FLOWCOUNT_TEST_ID" "$FLOWCOUNT_TEST_TS")"
mosquitto_pub -h localhost -u flowcount -P "$FLOWCOUNT_MQTT_PASSWORD" \
  -q 1 -t fabrica/setorA/bancada/TESTE-MANUAL/evento -m "$FLOWCOUNT_TEST_PAYLOAD"
```

A publicação deve encerrar sem erro. Para inspecionar as mensagens MQTT em outro terminal, leia novamente a senha nesse terminal e execute:

```bash
read -r -s -p 'Senha MQTT de flowcount: ' FLOWCOUNT_MQTT_PASSWORD
printf '\n'
mosquitto_sub -h localhost -u flowcount -P "$FLOWCOUNT_MQTT_PASSWORD" \
  -t 'fabrica/+/bancada/+/evento' -v
```

O assinante mostra apenas mensagens publicadas enquanto está conectado; encerre com `Ctrl+C`. No terminal da publicação, aguarde alguns segundos e consulte o evento:

```bash
docker compose exec -T postgres psql -U saap -d saap \
  -c "SELECT bancada, evt_id, ocorrido_em, delta FROM producao WHERE bancada = 'TESTE-MANUAL' AND evt_id = '$FLOWCOUNT_TEST_ID';"
```

**Resultado esperado:** uma linha, com o identificador recém-gerado e `delta = 1`. Reenvie o mesmo payload e confira a deduplicação:

```bash
mosquitto_pub -h localhost -u flowcount -P "$FLOWCOUNT_MQTT_PASSWORD" \
  -q 1 -t fabrica/setorA/bancada/TESTE-MANUAL/evento -m "$FLOWCOUNT_TEST_PAYLOAD"
```

Após o processamento:

```bash
docker compose exec -T postgres psql -U saap -d saap \
  -c "SELECT count(*) AS registros FROM producao WHERE bancada = 'TESTE-MANUAL' AND evt_id = '$FLOWCOUNT_TEST_ID';"
```

**Resultado esperado:** `registros = 1`. A saída bem-sucedida do publicador, sozinha, não comprova gravação no banco.

### 1.8 Confirmar o Grafana

1. Abra `http://<ip-da-raspberry>:3000`.
2. Entre com usuário `admin` e **GRAFANA_PASSWORD** do `.env`.
3. Em **Connections → Data sources**, abra **PostgreSQL-SAAP** e execute **Save & test**. A conexão deve ser aprovada.
4. Em **Dashboards → SAAP**, abra **SAAP — Produção em tempo real**.
5. Selecione um intervalo recente, como os últimos 15 minutos, e atualize o painel. O evento `TESTE-MANUAL` deve contribuir para os totais por bancada/período; painéis limitados a B01–B03 podem não mostrá-lo.

A validação precisa passar pelo banco **e** pelo painel antes de avançar para a estação física.

### 1.9 Simular produção sem a placa (opcional)

Na Raspberry, na pasta do servidor:

```bash
python3 -m venv .venv
. .venv/bin/activate
python -m pip install 'paho-mqtt==2.1.0'
read -r -s -p 'Senha MQTT de flowcount: ' MQTT_PASSWORD
printf '\n'
export MQTT_PASSWORD
export MQTT_USER=flowcount
python simulador/simula_bancadas.py --broker localhost --bancadas TESTE-SIM --intervalo 2
```

Deixe executar por cerca de 30 segundos e acompanhe os registros no banco e no Grafana. Pare com `Ctrl+C`, depois:

```bash
unset MQTT_PASSWORD MQTT_USER
deactivate
```

O simulador reinicia a sequência de `evt_id` em cada execução. Use um nome novo, como `TESTE-SIM-02`, para um novo ensaio; repetir o mesmo nome pode fazer o banco deduplicar eventos. Pare o simulador antes de medir a contagem física.


## 2. Preparar o ambiente do firmware

Execute desta seção em diante **no computador de desenvolvimento**, salvo quando indicado. Monte a estação conforme o [guia de hardware](hardware.md) e confira a [lista de materiais](materiais.md) antes de alimentar o circuito. Os testes automatizados podem ser executados sem a placa, seguindo o [guia de testes](../tests/README.md).

### 2.1 Instalar dependências do ESP-IDF

No Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y \
  git wget flex bison gperf \
  python3 python3-pip python3-venv \
  cmake ninja-build ccache \
  libffi-dev libssl-dev \
  dfu-util libusb-1.0-0
```

### 2.2 Instalar o ESP-IDF 5.5.5

Crie uma pasta para o framework:

```bash
mkdir -p ~/esp
cd ~/esp
```

Clone exatamente a versão utilizada pelo projeto:

```bash
git clone -b v5.5.5 --recursive https://github.com/espressif/esp-idf.git
```

Instale as ferramentas necessárias para ESP32-S3:

```bash
cd ~/esp/esp-idf
./install.sh esp32s3
```

Ative o ambiente:

```bash
. ~/esp/esp-idf/export.sh
```

Confirme:

```bash
idf.py --version
```

O resultado deve indicar ESP-IDF 5.5.5.

Sempre que abrir um novo terminal para trabalhar no firmware, ative novamente o ambiente com:

```bash
. ~/esp/esp-idf/export.sh
```

### 2.3 Clonar o FlowCount

```bash
mkdir -p ~/projetos
cd ~/projetos
git clone https://github.com/joaohenriquebitu/FlowCount.git
cd FlowCount
```

O código de referência conferido neste guia é a revisão `c7f9f8405eaf07489e9a738111b7240adb02af09`. Para compilar exatamente essa revisão:

```bash
git checkout --detach c7f9f8405eaf07489e9a738111b7240adb02af09
git rev-parse HEAD
```

Esse checkout pode conter uma edição anterior da documentação; mantenha este guia aberto. Se optar por uma revisão posterior, registre seu hash e repita as validações.

### 2.4 Selecionar o microcontrolador

Dentro do diretório do projeto:

```bash
idf.py set-target esp32s3
```

O target deve permanecer `esp32s3`.

## 3. Configurar o firmware

Abra o menu de configuração:

```bash
idf.py menuconfig
```

Configure as seções `FlowCount`. Para o fluxo completo, mantenha `FLOWCOUNT_COMM_ENABLED=y` e `FLOWCOUNT_DIAGNOSTIC_CONSUMER=n`: o consumidor de diagnóstico remove eventos localmente e não os transmite.

### 3.1 Eventos de produção

Defina pelo menos:

```text
FLOWCOUNT_STATION_ID: 1
FLOWCOUNT_BANCADA: B01
FLOWCOUNT_EVENT_QUEUE_CAPACITY: 72
```

Se houver várias estações, cada ESP32 deve possuir identificação coerente com a bancada física correspondente.

### 3.2 Horário

A configuração padrão utiliza:

```text
NTP server: pool.ntp.org
Clock max age: 86400 s
```

O ESP32 precisa de um horário válido para gerar eventos com timestamp adequado para o servidor.

### 3.3 Wi-Fi

Preencha:

```text
Wi-Fi SSID: <nome-da-rede>
Wi-Fi password: <senha-da-rede>
```

A rede precisa permitir que a Heltec alcance o endereço da Raspberry Pi.

### 3.4 MQTT

Configure o broker usando o IP da Raspberry:

```text
MQTT URI: mqtt://<ip-da-raspberry>:1883
```

Exemplo:

```text
mqtt://192.168.1.50:1883
```

Preencha também `FLOWCOUNT_MQTT_USERNAME=flowcount` e `FLOWCOUNT_MQTT_PASSWORD` com a senha criada no passo 1.4. O broker desta configuração recusa conexões anônimas. Use o IP da Raspberry na URI do firmware, não o nome interno `mosquitto`.

O Wi-Fi deve ser de 2,4 GHz e permitir acesso ao broker e ao servidor SNTP configurado (DNS e UDP 123). Sem horário válido, os eventos não são publicados.

### 3.5 Buzzer e LEDs

A configuração atual usa:

```text
GPIO: 45
Frequência: 2400 Hz
```

O GPIO deve comandar o transistor do circuito, não alimentar diretamente o buzzer.

Os LEDs possuem saídas próprias: **verde no GPIO 1**, com pulso de **100 ms** por contagem confirmada após liberar o sensor, e **vermelho no GPIO 40**, aceso desde a inicialização enquanto Wi-Fi ou MQTT estiver desconectado. O verde continua indicando contagens offline; o vermelho apaga quando as duas conexões estão prontas. Com `FLOWCOUNT_COMM_ENABLED=n`, o vermelho fica apagado.

Cada LED deve ter seu próprio resistor em série e cátodo no GND. Retire a ligação antiga do LED ao estágio do buzzer. Consulte a [montagem e as particularidades dos GPIOs](hardware.md#leds-independentes).

### 3.6 Salvar

Salve as opções no `menuconfig` e saia.

O ESP-IDF gerará um `sdkconfig` local. Esse arquivo contém configurações da máquina e pode conter credenciais de rede. Ele não deve ser enviado para repositórios públicos.

## 4. Compilar e gravar a Heltec

### 4.1 Compilar

```bash
idf.py build
```

Ao final, a compilação deve terminar sem erros. Antes de gravar, execute os [testes de host](../tests/README.md#executar-todos-os-testes). Compilar valida o firmware para ESP32-S3; os testes de host verificam a lógica com APIs simuladas.

### 4.2 Conectar a placa

Conecte a Heltec ao computador utilizando um cabo USB com suporte a dados.

No Linux, procure a porta serial:

```bash
ls /dev/serial/by-id/
```

ou:

```bash
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

Exemplo de porta:

```text
/dev/ttyACM0
```

Se houver problema de permissão serial, adicione seu usuário ao grupo apropriado:

```bash
sudo usermod -aG dialout $USER
```

Depois, encerre a sessão do usuário e entre novamente.

### 4.3 Gravar o firmware

Substitua a porta pelo dispositivo encontrado:

```bash
idf.py -p /dev/ttyACM0 flash
```

### 4.4 Abrir o monitor serial

```bash
idf.py -p /dev/ttyACM0 monitor
```

Também é possível gravar e abrir o monitor em um único comando:

```bash
idf.py -p /dev/ttyACM0 flash monitor
```

Para sair do monitor do ESP-IDF:

```text
Ctrl + ]
```

## 5. Verificar o sistema completo

Depois que o servidor estiver ativo e o firmware estiver gravado:

1. confirme que a Raspberry Pi está ligada e conectada à rede;
2. confirme que os containers estão ativos com `docker compose ps`;
3. ligue a estação FlowCount;
4. acompanhe o monitor serial do ESP32;
5. aguarde a conexão Wi-Fi;
6. aguarde os logs `Wi-Fi conectado`, `MQTT conectado` e `CLOCK_SYNCED`;
7. passe uma peça pelo sensor e retire-a completamente da região de detecção;
8. confirme a sinalização local da contagem;
9. abra `http://<ip-da-raspberry>:3000`;
10. acesse o dashboard do projeto;
11. verifique se a nova passagem aparece nos indicadores de produção.

Se o ESP32 contar localmente, mas o Grafana não atualizar, faça o diagnóstico por camadas:

```text
Sensor -> ESP32 -> Wi-Fi -> Mosquitto -> Node-RED -> PostgreSQL -> Grafana
```

Não altere vários componentes ao mesmo tempo. Primeiro confirme onde o evento deixa de aparecer e então consulte a documentação específica daquele módulo.

Para aprovar a instalação, execute os [ensaios de bancada e integração](../tests/README.md#ensaios-de-bancada-e-integração), com contagem controlada, peça parada e recuperação de conexão. Consulte o [diagnóstico do servidor](raspberry.md#diagnóstico-por-camada) se a publicação não chegar ao painel.

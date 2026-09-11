# MQTT → Telegraf → InfluxDB → Grafana

Repositório examinado: https://github.com/ValdimiroAlves/Grafana_Dashboards

## ESP32: repetição corrigida

A tarefa publicava a cabeça da fila a cada segundo sem removê-la. Agora:

1. Sem objeto novo e sem backlog, não publica nada.
2. Cada evento é copiado para o outbox do ESP-MQTT com QoS 1 e `retain=false`.
3. Somente se o outbox aceitar o payload, remove o evento da fila da aplicação.
4. Se offline ou sem espaço, preserva o evento para nova tentativa.
5. Retransmissões MQTT até PUBACK são responsabilidade do SDK.

Há envio do backlog ao reconectar, mesmo sem uma contagem naquele instante.
QoS 1 admite duplicatas de transporte; não é garantia de entrega exatamente uma
vez ou de persistência no InfluxDB. O outbox é RAM e pode perder mensagens no
reboot ou por expiração do SDK. A retirada da fila significa transferência de
responsabilidade para o SDK, não um ACK da Raspberry. O tópico usa a bancada
configurada em menuconfig, em vez de fixar B01 no código.

## Resultado do teste com as configurações do servidor

Uma cópia isolada de Mosquitto 2, Telegraf 1.30 e InfluxDB 1.8 foi iniciada com as
configurações do repositório. Publicado um JSON no formato do firmware:

```json
{"bancada":"B01","ts":"<UTC atual com microssegundos e Z>","delta":1}
```

O evento foi gravado em `saap.producao` com `bancada=B01`, `delta=1` e o timestamp
original. A consulta `sum(delta)` retornou **1**. A consulta `last(total_turno)`
não retornou linhas porque esse campo não é enviado pelo firmware.

O formato `.123000Z` é aceito pelo `json_time_format` atual. A ausência de
`evt_id`, `origem`, `total_hora` e `total_turno` não impede a ingestão de `delta`.
Portanto, o repositório por si só não explica TODOS os painéis vazios: é necessário
verificar os containers/configurações realmente implantados e o horário dos dados.
O Grafana consulta o InfluxDB; não recebe diretamente a mensagem do ESP32.

## Logs recebidos da Raspberry

Os logs enviados mostram `JSON time key could not be found` até
2026-09-11 00:57:07 UTC: essas mensagens foram rejeitadas na entrada do Telegraf
porque a chave temporal configurada não foi encontrada. Isso ocorre antes da
gravação no InfluxDB, não no Grafana. Após o reinício às 00:59:17 UTC aparece
conexão ao broker sem novos erros no trecho fornecido; é necessário capturar uma
mensagem e logs atuais para confirmar a situação presente.

O firmware deste checkout emite `ts`. Compare o payload recebido com
`json_time_key` dentro do container em execução. Se estiver diferente de `ts`,
alinhe a configuração com `telegraf/telegraf.conf` do repositório e reinicie o
Telegraf. Se a mensagem não contiver `ts`, confira qual firmware/dispositivo a
publicou e grave a versão atual no ESP32. Não remova a configuração de horário
para mascarar o erro: isso trocaria ocorrência por recebimento.

## Corrigir o painel incompatível

O arquivo `grafana-periodo.patch` altera somente o painel da tabela:

- Título: `Total no período por bancada`.
- Consulta: `SELECT sum("delta") FROM "producao" WHERE $timeFilter GROUP BY "bancada"`.

Não se inventa um total de turno no ESP32: um total desde o boot não é um total
de turno. Para visualizar um turno, selecione seu intervalo no Grafana.

Copie o patch para a Raspberry. Na pasta do repositório `Grafana_Dashboards`:

```bash
git apply --check /caminho/grafana-periodo.patch
git apply /caminho/grafana-periodo.patch
docker compose restart grafana
```

O patch foi preparado localmente; não foi publicado no repositório remoto nem
aplicado na Raspberry. O restante dos painéis já usa `delta`.

## Se os demais painéis estiverem vazios

Execute na pasta do projeto da Raspberry:

```bash
date -u
docker compose ps
docker compose logs --tail=80 telegraf
docker compose exec -T influxdb influx -database saap -execute 'SHOW MEASUREMENTS'
docker compose exec -T influxdb influx -database saap -execute 'SELECT * FROM "producao" ORDER BY time DESC LIMIT 5'
docker compose exec -T influxdb influx -database saap -execute 'SELECT sum("delta") FROM "producao" WHERE time > now() - 12h GROUP BY "bancada"'
docker compose logs --tail=80 grafana
```

- Se não houver measurement/pontos: confira logs de conexão, parsing e gravação
  do Telegraf. O subscriber deve apontar para o MESMO broker que recebe o ESP32
  e assinar `fabrica/+/bancada/+/evento`.
- Se existirem pontos, mas a soma das últimas 12 h vier vazia: compare o UTC do
  payload com a data da Raspberry e o período do dashboard. Repetir uma mensagem
  antiga não atualiza seu timestamp; ela pode permanecer fora da janela exibida.
- Se a soma vier preenchida: confira a fonte `InfluxDB-SAAP`, UID `saap-influx`,
  banco `saap`, URL interna `http://influxdb:8086`, o teste da fonte no Grafana e
  se o dashboard aberto é o provisionado por este repositório.
- No firmware, `Falha ao serializar evento MQTT` indica, entre outras causas,
  evento sem UTC válido (`clock_synced=0`). Não se altera retroativamente o horário
  de ocorrência. Esse evento permanece na fila e requer diagnóstico separado;
  não deve ser confundido com falha de parsing de um payload recebido no broker.

Não apague volumes/bancos para diagnosticar: isso eliminaria o histórico.

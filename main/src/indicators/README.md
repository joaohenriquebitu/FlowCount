# Sinalização local

A sinalização implementada no firmware atual é concentrada no módulo do buzzer. O objetivo é fornecer retorno imediato ao operador sem bloquear a tarefa de contagem.

Voltar para a [documentação do firmware](../../README.md) ou para o [README principal](../../../README.md).

## Arquivos

```text
main/
├── include/indicators/buzzer.h
└── src/indicators/buzzer.c
```

## Hardware controlado

O firmware controla um buzzer passivo KC-1206 por PWM. No protótipo atual:

| Parâmetro | Valor padrão |
|---|---:|
| GPIO de comando | 45 |
| Frequência PWM | 2400 Hz |
| Resolução LEDC | 10 bits |
| Duty durante o som | 512/1023, aproximadamente 50% |
| Timer LEDC | 0 |
| Canal LEDC | 0 |

O GPIO não deve alimentar o buzzer diretamente. Ele deve comandar um transistor externo. Consulte [docs/hardware.md](../../../docs/hardware.md) e [docs/buzzer.md](../../../docs/buzzer.md).

## Comportamentos sonoros

| Evento | Sinal |
|---|---|
| passagem válida | 1 beep de aproximadamente 60 ms |
| queda de Wi-Fi ou MQTT após já estar conectado | 3 pulsos de aproximadamente 180 ms |
| boot sem conexão | nenhum alerta |
| primeira tentativa de conexão | nenhum alerta |
| mesma indisponibilidade ainda ativa | não repete continuamente |

O alerta de queda é rearmado quando Wi-Fi e MQTT voltam a estar conectados.

## Arquitetura não bloqueante

`buzzer_beep()` não toca o som diretamente. A chamada apenas incrementa uma fila lógica de beeps pendentes e retorna imediatamente.

O PWM é controlado exclusivamente pelo callback periódico de um `esp_timer` com período de 10 ms.

```mermaid
flowchart LR
    COUNT["Contagem"] -->|buzzer_beep| QUEUE["beeps pendentes"]
    WIFI["Wi-Fi"] -->|connection_changed| ALERT["alerta pendente"]
    MQTT["MQTT"] -->|connection_changed| ALERT
    TIMER["esp_timer / 10 ms"] --> FSM["sequenciador"]
    QUEUE --> FSM
    ALERT --> FSM
    FSM --> LEDC["LEDC PWM"]
    LEDC --> Q1["transistor"]
    Q1 --> BUZZER["KC-1206"]
```

Essa abordagem evita `vTaskDelay()` dentro da lógica de contagem e impede que callbacks de rede fiquem esperando um som terminar.

## Máquina de fases do buzzer

Internamente existem as fases:

```text
IDLE
COUNT_ON
COUNT_GAP
ALERT_ON
ALERT_GAP
```

Tempos nominais:

| Fase | Duração |
|---|---:|
| beep de contagem | 60 ms |
| intervalo entre beeps de contagem | 40 ms |
| pulso de alerta | 180 ms |
| intervalo do alerta | 120 ms |
| tick do sequenciador | 10 ms |

As durações são nominais. Um atraso de escalonamento pode estender um pulso, porque o callback não é uma fonte de temporização hard real-time.

## Prioridade entre sons

Alertas de conectividade têm prioridade sobre beeps de contagem pendentes.

Se ocorrer uma contagem durante os três pulsos do alerta:

1. a contagem e o evento são processados normalmente;
2. `buzzer_beep()` registra o beep pendente;
3. o beep é tocado depois que o alerta termina.

Uma contagem durante outro beep não prolonga o som atual; ela gera outro beep separado.

## Fila de beeps

O limite é:

```text
MAX_PENDING_BEEPS = 16
```

Se a fila lógica já possuir 16 beeps aguardando, `buzzer_beep()` retorna `ESP_ERR_NO_MEM`. O erro de sinalização não desfaz a contagem nem o evento de produção.

## Detecção de queda de conectividade

O módulo recebe notificações por:

```c
buzzer_connection_changed(BUZZER_CONNECTION_WIFI, connected);
buzzer_connection_changed(BUZZER_CONNECTION_MQTT, connected);
```

Uma queda só é considerada quando uma camada que estava conectada muda para desconectada. Assim, o boot offline não é interpretado como falha.

Como Wi-Fi e MQTT normalmente caem juntos, `outage_reported` agrupa o episódio em um único alerta. Quando ambas as camadas voltam a ficar conectadas, um novo episódio pode gerar outro alerta.

## Falhas do driver

Se `ledc_set_duty()` ou `ledc_update_duty()` falhar:

1. o erro é registrado;
2. o módulo tenta `ledc_stop()` para forçar a saída em LOW;
3. o sequenciador retorna para estado ocioso;
4. uma falha posterior de `ledc_stop()` é tentada novamente no próximo tick.

A aplicação não deve considerar o som como confirmação de que a contagem foi persistida no servidor.

## LED vermelho

O firmware desta revisão **não possui um módulo de LED nem um GPIO dedicado para LED**.

No esboço físico/Wokwi atual, o LED vermelho aparece associado ao mesmo estágio de transistor do buzzer. Nesse arranjo, ele funciona como indicação elétrica do acionamento do estágio, e não como um indicador independente controlado por software.

Se o projeto passar a exigir estados visuais próprios, como "online", "fila cheia" ou "erro", recomenda-se criar um módulo específico e reservar um GPIO independente.

## Configuração

No menuconfig:

```text
FlowCount - buzzer KC-1206
```

Opções:

- `FLOWCOUNT_BUZZER_GPIO`, padrão 45;
- `FLOWCOUNT_BUZZER_FREQUENCY_HZ`, padrão 2400 Hz.

GPIO45 é um pino de strapping do ESP32-S3. O circuito externo não deve forçar um nível inadequado durante reset/boot. A montagem deve ser validada na placa Heltec utilizada.

## Validação em bancada

1. ligue o sistema e confirme no serial o log de inicialização do buzzer;
2. não espere beep durante o boot;
3. passe uma peça válida e confirme um beep curto;
4. mantenha uma peça parada no sensor e confirme que não surgem beeps repetidos;
5. com Wi-Fi e MQTT conectados, derrube o broker e confirme três pulsos;
6. mantenha a indisponibilidade e confirme que o alarme não fica repetindo;
7. reconecte tudo, derrube novamente e confirme que o alerta foi rearmado;
8. se não houver som, meça primeiro o GPIO/PWM e depois o estágio do transistor.

## Testes relacionados

- `tests/test_buzzer.c` valida PWM, duração, alertas, reconexão, fila e falhas do driver;
- `tests/test_network_alerts.c` valida a integração das notificações de Wi-Fi/MQTT.

Consulte [tests/README.md](../../../tests/README.md).

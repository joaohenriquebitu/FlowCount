# Buzzer KC-1206

Este documento reúne o comportamento de software e os requisitos de montagem do buzzer usado pelo FlowCount. Para a lógica do módulo, consulte também [main/src/indicators/README.md](../main/src/indicators/README.md).

## Comportamento implementado

| Evento | Som |
|---|---|
| nova contagem confirmada | 1 beep de aproximadamente 60 ms |
| Wi-Fi ou MQTT passa de conectado para desconectado | 3 pulsos de aproximadamente 180 ms, separados por 120 ms |
| boot, primeira tentativa de conexão ou reconexão | silêncio |
| mesma indisponibilidade continua | não repete continuamente |

O alerta de rede é rearmado depois que Wi-Fi e MQTT estiverem novamente conectados.

## Configuração do firmware

No menu:

```text
FlowCount - buzzer KC-1206
```

existem duas opções:

| Opção | Padrão |
|---|---:|
| `FLOWCOUNT_BUZZER_GPIO` | 45 |
| `FLOWCOUNT_BUZZER_FREQUENCY_HZ` | 2400 Hz |

O PWM usa aproximadamente 50% de duty e é gerado pelo LEDC.

## Circuito de acionamento

O projeto utiliza:

- buzzer KC-1206;
- transistor NPN 2N2222A-1726;
- resistor de 2 kΩ entre o GPIO e a base;
- GND comum.

Ligação funcional:

```text
Heltec GPIO45
      |
     2 kΩ
      |
      B
   2N2222A
 C        E
 |        |
 |       GND
 |
 +---- negativo do buzzer
       positivo do buzzer -> alimentação compatível
```

O GPIO não deve alimentar diretamente a carga.

A pinagem de base, coletor e emissor deve ser confirmada no datasheet do transistor efetivamente utilizado. Encapsulamentos com aparência semelhante podem ter ordem de pinos diferente.

## Proteção da carga

Se o buzzer utilizado for um transdutor magnético/indutivo, a etapa de potência deve prever a proteção adequada contra transientes. Um diodo de flyback é normalmente utilizado sobre a carga, com orientação que permaneça reversamente polarizada durante a alimentação normal.

A necessidade e o dimensionamento devem ser conferidos com a especificação da unidade instalada.

## GPIO45 e boot

GPIO45 é um pino de strapping do ESP32-S3. O circuito externo deve permitir que a placa inicialize normalmente e não pode forçar nível alto indevido durante reset.

Se surgirem problemas de boot depois de conectar o estágio do buzzer, desconecte temporariamente a base/transistor e valide a influência do circuito externo.

## Sequenciador de som

Depois de `buzzer_init()`, somente o callback periódico do `esp_timer` altera o PWM.

Fases internas:

```text
IDLE -> COUNT_ON -> COUNT_GAP
IDLE -> ALERT_ON -> ALERT_GAP -> ALERT_ON -> ...
```

O timer é chamado a cada 10 ms. Os produtores apenas enfileiram notificações.

Há capacidade para até 16 beeps de contagem pendentes. Excesso retorna `ESP_ERR_NO_MEM` sem interferir na contagem de peças.

## LEDs independentes

Os LEDs verde e vermelho são controlados pelo módulo `leds`, com saídas próprias: GPIO1 para o verde e GPIO40 para o vermelho. Retire a ligação antiga do LED ao estágio do transistor do buzzer. Cada LED recebe seu próprio resistor em série e tem o cátodo ligado ao GND, conforme [hardware.md](hardware.md#leds-independentes).

O verde pulsa por 100 ms em cada contagem válida, inclusive offline. O vermelho permanece aceso enquanto Wi-Fi ou MQTT estiver desconectado, inclusive ao iniciar; apaga com ambas as conexões prontas e fica apagado se a comunicação estiver desabilitada. Os beeps de contagem e os três pulsos de alerta permanecem iguais. O vermelho continua aceso depois do fim do alerta sonoro, até a conexão voltar.

## Ensaio na placa

1. com a montagem desligada, confira transistor, resistor de 2 kΩ, polaridade do buzzer e continuidade do GND; confirme que os LEDs estão em GPIOs independentes;
2. energize e confirme a tensão de alimentação real do buzzer;
3. grave o firmware e abra o monitor serial;
4. confirme um log semelhante a `KC-1206: GPIO=45 PWM=2400 Hz; contagem=60 ms; alerta=3 pulsos`;
5. não espere beep no boot;
6. faça uma passagem válida pelo sensor e confirme um beep curto;
7. com Wi-Fi e MQTT conectados, derrube o broker e confirme três pulsos;
8. mantenha o broker indisponível e confirme que o alerta não fica repetindo;
9. reconecte e derrube novamente para confirmar o rearme;
10. realize uma contagem durante o alerta e confirme que o evento ocorre e o beep é reproduzido depois.

## Se não houver som

Separe software de hardware:

1. confirme no log que `buzzer_init()` não falhou;
2. meça GPIO45 com analisador lógico/osciloscópio;
3. durante o beep deve existir PWM entre aproximadamente 0 e 3,3 V a 2,4 kHz;
4. se o GPIO está correto, meça a base e o coletor do transistor;
5. confirme a tensão realmente aplicada ao buzzer;
6. confirme GND comum;
7. confirme a pinagem B/C/E;
8. teste o buzzer separadamente dentro de sua especificação.

Um multímetro pode ajudar a verificar alimentação e continuidade, mas não é suficiente para validar frequência e duty do PWM.

## Testes de software

`tests/test_buzzer.c` verifica:

- falhas em cada etapa da inicialização;
- idempotência de `buzzer_init()`;
- beep de 60 ms;
- separação entre beeps consecutivos;
- alerta de três pulsos;
- agrupamento de quedas Wi-Fi/MQTT;
- rearme após reconexão;
- fila de 16 beeps;
- falhas de `ledc_set_duty`, `ledc_update_duty` e `ledc_stop`.

`tests/test_network_alerts.c` verifica a integração das mudanças de estado da rede com o módulo.

Esses testes não medem volume, frequência física real ou a montagem elétrica.

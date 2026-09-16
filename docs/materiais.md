# Materiais necessários

[Voltar ao README principal](../README.md)

A tabela abaixo considera a reprodução de **uma estação de contagem** conectada a um servidor central. Para monitorar várias bancadas, replique os componentes marcados como "por estação".

| Item | Quantidade | Função | Observação |
|---|---:|---|---|
| Heltec WiFi LoRa 32 V3 | 1 por estação | Executar o firmware do FlowCount | Placa baseada em ESP32-S3; LoRa não é utilizado no fluxo atual |
| Sensor fotoelétrico E18-D80NK | 1 por estação | Detectar a passagem das peças | Entrada de contagem utilizada pelo firmware |
| Buzzer KC-1206 | 1 por estação | Sinalização sonora | Acionado por PWM através de transistor |
| Transistor NPN 2N2222A-1726 | 1 por estação | Acionamento do buzzer | Evita alimentar a carga diretamente pelo GPIO |
| Resistor 100 kΩ | 3 por estação | Interface do sensor | Valores utilizados no protótipo atual |
| Resistor 2 kΩ | 1 por estação | Interface do transistor/buzzer | Utilizado no comando do transistor |
| Resistor 220 Ω | 2 por estação | Limitação de corrente dos LEDs | Um resistor em série com cada LED; valor de referência do protótipo |
| LED verde | 1 por estação | Pulso em cada passagem válida | GPIO 1 por padrão |
| LED vermelho | 1 por estação | Indicar Wi-Fi ou MQTT desconectado | GPIO 40 por padrão |
| Protoboard ou placa de montagem | 1 por estação | Montagem do circuito | Para protótipo; em versão final pode ser substituída por PCB |
| Jumpers/fios de conexão | Conforme necessário | Interligação elétrica | Macho-macho, macho-fêmea ou conforme a montagem |
| Cabo USB de dados para a Heltec | 1 por estação | Alimentação, gravação e monitor serial | Deve permitir transferência de dados |
| Fonte/linha de 5 V adequada | 1 por estação | Alimentar os componentes de 5 V | Utilizar GND comum entre os elementos da estação |
| Diodo de proteção para carga indutiva | 1 por estação | Proteção do acionamento do buzzer | Recomendado para a montagem final; dimensionar conforme o componente usado |
| Esteira ou estrutura de passagem | 1 | Movimentar as peças pelo ponto de leitura | Pode ser substituída por passagem manual durante testes |
| Raspberry Pi 5 | 1 por instalação | Executar o servidor central | Um único servidor pode atender várias estações |
| Fonte USB-C 27 W para Raspberry Pi 5 | 1 | Alimentar a Raspberry Pi | A documentação do servidor recomenda fonte adequada à Pi 5 |
| Cooler ativo para Raspberry Pi 5 | 1 | Refrigeração | Recomendado para operação contínua |
| Cartão microSD A2/V30 de 128 GB | 1 | Sistema operacional e dados da Raspberry Pi | Configuração de referência do servidor; SSD USB é uma alternativa para uso contínuo |
| Roteador ou ponto de acesso Wi-Fi | 1 | Comunicação entre ESP32 e servidor | O ESP32 e a Raspberry precisam alcançar a mesma infraestrutura de rede |
| Computador de desenvolvimento | 1 | Compilar e gravar o firmware | Linux é o ambiente descrito neste README |

Consulte as ligações elétricas no [guia de hardware e montagem](hardware.md).

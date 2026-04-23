#include <Arduino.h>

const int pinRelay = 14; 
const int pinZMPT = GPIO_NUM_15;        // GPIO3 conectado a OUT del ZMPT101B
const int muestras = 500;     // número de muestras para RMS
const float VCC = 3.3;        // alimentación del ADC del ESP32 (máx. 3.3V)
float offsetADC = 0;          // offset inicial (≈ VCC/2 en ADC)
float scaleFactor = 1.0;      // factor de calibración con multímetro

void setup() {
  Serial.begin(115200);
  pinMode(pinRelay, OUTPUT);
  pinMode(pinZMPT, INPUT);
  digitalWrite(pinRelay, HIGH);
  // Medir offset inicial (promedio de lecturas sin carga)
  long suma = 0;
  for (int i = 0; i < 1000; i++) {
    suma += analogRead(pinZMPT);
    delay(1);
  }
  offsetADC = suma / 1000.0;

  Serial.print("Offset ADC: ");
  Serial.println(offsetADC);
  Serial.println("Iniciando medición RMS...");
}

void loop() {
  // Calcular RMS de la señal
  double sumaCuadrados = 0;
  for (int i = 0; i < muestras; i++) {
    int lectura = analogRead(pinZMPT);
    float valor = lectura - offsetADC;   // quitar offset
    sumaCuadrados += valor * valor;
    delay(2); // ajustar según frecuencia de muestreo
  }

  float rmsADC = sqrt(sumaCuadrados / muestras);

  // Convertir a voltaje del sensor
  float voltajeSensor = (rmsADC / 4095.0) * VCC;

  // Aplicar factor de calibración (ajustar con multímetro)
  float voltajeReal = voltajeSensor * scaleFactor;

  Serial.print("RMS ADC: ");
  Serial.print(rmsADC);
  Serial.print("  Voltaje Sensor: ");
  Serial.print(voltajeSensor, 3);
  Serial.print(" V  Voltaje Real: ");
  Serial.print(voltajeReal, 2);
  Serial.println(" V");

  delay(1000);
}
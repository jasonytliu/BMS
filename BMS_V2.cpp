/** BMS V2. Cell Balancing
* See README.md for more information on contents of V2. 
*/

//INSERT CODE HERE

void setup()
{
  //Runs once on startup
}

void loop()
{
  //Runs forever
}



/***********************************************/
/************** Available functions ************/
/***********************************************/
/*
  displayVoltages()
  enableBalancing()
  enterSHIPmode() - sets BMS to low power mode
  initBQ() - Verifies communication. Reads gain and offset.
  readADCoffset()
  readCellVoltage()
  readCoulombCounter()
  readGAIN()
  readOVtrip() - read overvoltage setting
  readPackVoltage()
  readTemp()
  readUVtrip() - read undervoltage setting
  registerDoubleRead()
  registerRead()
  registerWrite(register, data) - overwrites entire register with "data"
  thermistorLookup()
  tripCalculator() - calculates 8 bit value to write to register
  writeOVtrip() - set overvoltage
  writeUVtrip() - set undervoltage
*/

// this is irq handler for bq769x0 interrupts, has to return void and take no arguments
// always make code in interrupt handlers fast and short
void bq769x0IRQ()
{
  bq769x0_IRQ_Triggered = true;
}

//Initiates the first few I2C commands
//Returns true if we can verify communication
//Set CC_CFG to default 0x19
//Turn on the ADC
//Assume we are checking internal die temperatures (leave TEMP_SEL at zero)
//Configure the interrupts for Arduino Uno
//Read the Gain and Offset factory settings into global variables
boolean initBQ(byte irqPin)
{
  //Test to see if we have correct I2C communication
  //byte testByte = registerRead(bq796x0_OV_TRIP); //Should be something other than zero on POR
  byte testByte = registerRead(bq796x0_ADCGAIN2); //Should be something other than zero on POR

  for(byte x = 0 ; x < 10 && testByte == 0 ; x++)
  {
    Serial.print(".");
    testByte = registerRead(bq796x0_ADCGAIN2);
    delay(100);
  }
  //if(testByte == 0x00) return false; //Something is very wrong. Check wiring.

  //"For optimal performance, [CC_CFG] should be programmed to 0x19 upon device startup." page 40
  registerWrite(bq796x0_CC_CFG, 0x19); //address, value


  //Double check that ADC is enabled
  byte sysVal = registerRead(bq796x0_SYS_CTRL1);
  if(sysVal & bq796x0_ADC_EN)
  {
    Serial.println("ADC Already Enabled");
  }
  sysVal |= bq796x0_ADC_EN; //Set the ADC_EN bit
  registerWrite(bq796x0_SYS_CTRL1, sysVal); //address, value

  //Enable countinous reading of the Coulomb Counter
  sysVal = registerRead(bq796x0_SYS_CTRL2);
  sysVal |= bq796x0_CC_EN; //Set the CC_EN bit
  registerWrite(bq796x0_SYS_CTRL2, sysVal); //address, value
  //Serial.println("Coulomb counter enabled");

  //Attach interrupt
  pinMode(irqPin, INPUT); //No pull up

  if(irqPin == 2)
    //Interrupt zero on Uno is pin 2
    attachInterrupt(0, bq769x0IRQ, RISING);
  else if (irqPin == 3)
    //Interrupt one on Uno is pin 3
    attachInterrupt(1, bq769x0IRQ, RISING);
  else
    Serial.println("irqPin invalid. Alert IRQ not enabled.");

  //Gain and offset are used in multiple functions
  //Read these values into global variables
  gain = readGAIN() / (float)1000; //Gain is in uV so this converts it to mV. Example: 0.370mV/LSB
  offset = readADCoffset(); //Offset is in mV. Example: 65mV

  Serial.print("gain: ");
  Serial.print(gain);
  Serial.println("uV/LSB");

  Serial.print("offset: ");
  Serial.print(offset);
  Serial.println("mV");

  //Read the system status register
  byte sysStat = registerRead(bq796x0_SYS_STAT);
  if(sysStat & bq796x0_DEVICE_XREADY)
  {
    Serial.println("Device X Ready Error");
    //Try to clear it
    registerWrite(bq796x0_SYS_STAT, bq796x0_DEVICE_XREADY);

    delay(500);
    //Check again
    byte sysStat = registerRead(bq796x0_SYS_STAT);
    if(sysStat & bq796x0_DEVICE_XREADY)
    {
      Serial.println("Device X Ready Not Cleared");
    }
  }

  //Set any other settings such as OVTrip and UVTrip limits
  float under = readUVtrip();
  float over = readOVtrip();

  Serial.print("Undervoltage trip: ");
  Serial.print(under);
  Serial.println("V");

  Serial.print("Overvoltage trip: ");
  Serial.print(over);
  Serial.println("V");

  if(under != 3.32)
  {
    writeUVtrip(3.32); //Set undervoltage to 3.32V
    Serial.print("New undervoltage trip: ");
    Serial.print(readUVtrip());
    Serial.println("V"); //should print 3.32V
  }

  if(over != 4.27)
  {
    writeOVtrip(4.27); //Set overvoltage to 4.27V
    Serial.print("New overvoltage trip: ");
    Serial.print(readOVtrip());
    Serial.println("V"); //should print 4.27V
  }

  return true;
}

//Pretty print the pack voltages
void displayVoltages(void)
{
  Serial.println("Voltages:");

  for(int i = 1 ; i < NUMBER_OF_CELLS + 1 ; i++)
  {
    Serial.print("[");
    Serial.print(i);
    Serial.print("]");

    Serial.print(cellVoltage[i], 2);
    Serial.print(" ");

    if(i % 5 == 0) Serial.println();
  }
}

//Enable or disable the balancing of a given cell
//Give me a cell # and whether you want balancing or not
void enableBalancing(byte cellNumber, boolean enabled)
{
  byte startingBit, cellRegister;

  if(cellNumber < 1 || cellNumber > 15) return; //Out of range

  if(cellNumber < 6)
  {
    startingBit = 0;
    cellRegister = bq796x0_CELLBAL1;
  }
  else if(cellNumber < 11)
  {
    startingBit = 6; //The 2nd Cell balancing register starts at CB6
    cellRegister = bq796x0_CELLBAL2; //If the cell number is 6-10 then we are in the 2nd cell balancing register
  }
  else if(cellNumber < 16)
  {
    startingBit = 11;
    cellRegister = bq796x0_CELLBAL3;
  }

  byte cell = registerRead(cellRegister); //Read what is currently there

  if(enabled)
    cell |= (1<<(cellNumber - startingBit)); //Set bit for balancing
  else
    cell &= ~(1<<(cellNumber - startingBit)); //Clear bit to disable balancing

  registerWrite(cellRegister, cell); //Make it so
}

//Calling this function will put the IC into ultra-low power SHIP mode
//A boot signal is needed to get back to NORMAL mode
void enterSHIPmode(void)
{
  //This function is currently untested but should work
  byte sysValue = registerRead(bq796x0_SYS_CTRL1);

  sysValue &= 0xFC; //Step 1: 00
  registerWrite(bq796x0_SYS_CTRL1, sysValue);

  sysValue |= 0x03; //Step 2: non-01
  registerWrite(bq796x0_SYS_CTRL1, sysValue);

  sysValue &= ~(1<<1); //Step 3: 01
  registerWrite(bq796x0_SYS_CTRL1, sysValue);

  sysValue = (sysValue & 0xFC) | (1<<1); //Step 4: 10
  registerWrite(bq796x0_SYS_CTRL1, sysValue);

  //bq should now be in powered down SHIP mode and will not respond to commands
  //Boot on VS1 required to start IC
}

//Given a cell number, return the cell voltage
//Vcell = GAIN * ADC(cell) + OFFSET
//Conversion example from datasheet: 14-bit ADC = 0x1800, Gain = 0x0F, Offset = 0x1E = 2.365V
float readCellVoltage(byte cellNumber)
{
  if(cellNumber < 1 || cellNumber > 15) return(-0); //Return error

  //Serial.print("Read cell number: ");
  //Serial.println(cellNumber);

  //Reduce the caller's cell number by one so that we get register alignment
  cellNumber--;

  byte registerNumber = bq796x0_VC1_HI + (cellNumber * 2);

  //Serial.print("register: 0x");
  //Serial.println(registerNumber, HEX);

  int cellValue = registerDoubleRead(registerNumber);

  //int cellValue = 0x1800; //6,144 - Should return 2.365
  //int cellValue = 0x1F10l; //Should return 3.052

  //Cell value should now contain a 14 bit value

  //Serial.print("Cell value (dec): ");
  //Serial.println(cellValue);

  if(cellValue == 0) return(0);

  float cellVoltage = cellValue * gain + offset; //0x1800 * 0.37 + 60 = 3,397mV
  cellVoltage /= (float)1000;

  //Serial.print("Cell voltage: ");
  //Serial.println(cellVoltage, 3);

  return(cellVoltage);
}

//Given a thermistor number return the temperature in C
//Valid thermistor numbers are 1 to 3 for external and 0 to read the internal die temp
//If you switch between internal die and external TSs this function will delay 2 seconds
int readTemp(byte thermistorNumber)
{
  //There are 3 external thermistors (optional) and an internal temp reading (channel 0)
  if(thermistorNumber < 0 || thermistorNumber > 3) return(-0); //Return error

  //Serial.print("Read thermistor number: ");
  //Serial.println(thermistorNumber);

  byte sysValue = registerRead(bq796x0_SYS_CTRL1);

  if(thermistorNumber > 0)
  {
    //See if we need to switch between internal die temp and external thermistor
    if((sysValue & bq796x0_TEMP_SEL) == 0)
    {
      //Bad news, we have to do a switch and wait 2 seconds
      //Set the TEMP_SEL bit
      sysValue |= bq796x0_TEMP_SEL;
      registerWrite(bq796x0_SYS_CTRL1, sysValue); //address, value

        Serial.println("Waiting 2 seconds to switch thermistors");
      delay(2000);
    }

    int registerNumber = bq796x0_TS1_HI + ((thermistorNumber - 1) * 2);
    int thermValue = registerDoubleRead(registerNumber);

    //Therm value should now contain a 14 bit value

    Serial.print("Therm value: 0x");
    Serial.println(thermValue, HEX); //0xC89 = 3209

    float thermVoltage = thermValue * (float)382; //0xC89 * 382 = 1,225,838uV. 0x233C * 382uV/LSB = 3,445,640uV
    thermVoltage /= (float)1000000; //Convert to V

    Serial.print("thermVoltage: ");
    Serial.println(thermVoltage, 3);

    float thermResistance = ((float)10000 * thermVoltage) / (3.3 - thermVoltage);

    Serial.print("thermResistance: ");
    Serial.println(thermResistance);

    //We now have thermVoltage and resistance. With a datasheet for the NTC 103AT thermistor we could
    //calculate temperature.
    int temperatureC = thermistorLookup(thermResistance);

    Serial.print("temperatureC: ");
    Serial.println(temperatureC);

    return(temperatureC);
  }
  else if(thermistorNumber == 0)
  {
    //See if we need to switch between internal die temp and external thermistor
    if((sysValue & 1<<3) != 0)
    {
      //Bad news, we have to do a switch and wait 2 seconds
      //Clear the TEMP_SEL bit
      sysValue &= ~(1<<3);
      registerWrite(bq796x0_SYS_CTRL1, sysValue); //address, value

        Serial.println("Waiting 2 seconds to switch to internal die thermistors");
      delay(2000);
    }

    int thermValue = registerDoubleRead(bq796x0_TS1_HI); //There are multiple internal die temperatures. We are only going to grab 1.

    //Therm value should now contain a 14 bit value
    //Serial.print("Therm value: 0x");
    //Serial.println(thermValue, HEX);

    float thermVoltage = thermValue * (float)382; //0xC89 * 382 = 1,225,838uV. 0x233C * 382uV/LSB = 3,445,640uV
    thermVoltage /= (float)1000000; //Convert to V

    //Serial.print("thermVoltage: ");
    //Serial.println(thermVoltage, 3);

    float temperatureC = 25.0 - ((thermVoltage - 1.2) / 0.0042);

    //Serial.print("temperatureC: ");
    //Serial.println(temperatureC);

    //float temperatureF = (temperatureC * ((float)9/5)) + 32;

    //Serial.print("temperatureF: ");
    //Serial.println(temperatureF);

    return((int)temperatureC);
  }

}

//Returns the coulomb counter value in microVolts
//Example: 84,400uV
//Coulomb counter is enabled during bqInit(). We do not use oneshot.
//If the counter is enabled in ALWAYS ON mode it will set the ALERT pin every 250ms. You can respond to this however you want.
//Host may clear the CC_READY bit or let it stay at 1.
float readCoulombCounter(void)
{
  int count = registerDoubleRead(bq796x0_CC_HI);

  //int count = 0xC350; //Test. Should report -131,123.84

  float count_uV = count * 8.44; //count should be naturally in 2's compliment. count_uV is now in uV

  return(count_uV);
}

//Returns the pack voltage in volts
//Vbat = 4 * GAIN * ADC(cell) + (# of cells * offset)
float readPackVoltage(void)
{
  unsigned int packADC = registerDoubleRead(bq796x0_BAT_HI);

  //Serial.print("packADC = ");
  //Serial.println(packADC);

  //packADC = 0x6DDA; //28,122 Test. Should report something like 42.520V

  //packADC = 35507
  //gain = 0.38uV/LSB
  //offset = 47mV
  //53970
  float packVoltage = 4 * gain * packADC; //53970 in uV?
  packVoltage += (NUMBER_OF_CELLS * offset); //Should be in mV

  return(packVoltage / (float)1000); //Convert to volts
}

//Reads the gain registers and calculates the system's factory trimmed gain
//GAIN = 365uV/LSB + (ADCGAIN<4:0>) * 1uV/LSB
//ADC gain comes from two registers that have to be moved around and combined.
int readGAIN(void)
{
  byte val1 = registerRead(bq796x0_ADCGAIN1);
  byte val2 = registerRead(bq796x0_ADCGAIN2);
  val1 &= 0b00001100; //There are some unknown reservred bits around val1 that need to be cleared

  //Recombine the bits into one ADCGAIN
  byte adcGain = (val1 << 1) | (val2 >> 5);

  int gain = 365 + adcGain;

  return(gain);
}

//Returns the factory trimmed ADC offset
//Offset is -127 to 128 in mV
int readADCoffset(void)
{
  //Here we need to convert a 8bit 2's compliment to a 16 bit int
  char offset = registerRead(bq796x0_ADCOFFSET);

  return((int)offset); //8 bit char is now a full 16-bit int. Easier math later on.
}

//Returns the over voltage trip threshold
//Default is 0b.10.OVTRIP(0xAC).1000 = 0b.10.1010.1100.1000 = 0x2AC8 = 10,952
//OverVoltage = (OV_TRIP * GAIN) + ADCOFFSET
//Gain and Offset is different for each IC
//Example: voltage = (10,952 * 0.370) + 56mV = 4.108V
float readOVtrip(void)
{
  int trip = registerRead(bq796x0_OV_TRIP);

  trip <<= 4; //Shuffle the bits to align to 0b.10.XXXX.XXXX.1000
  trip |= 0x2008;

  float overVoltage = ((float)trip * gain) + offset;
  overVoltage /= 1000; //Convert to volts

  //Serial.print("overVoltage should be around 4.108: ");
  //Serial.println(overVoltage, 3);

  return(overVoltage);
}

//Given a voltage (4.22 for example), set the over voltage trip register
//Example: voltage = 4.2V = (4200mV - 56mV) / 0.370mv = 11,200
//11,200 = 0x2BC0 =
void writeOVtrip(float tripVoltage)
{
  byte val = tripCalculator(tripVoltage); //Convert voltage to an 8-bit middle value
  registerWrite(bq796x0_OV_TRIP, val); //address, value
}

//Returns the under voltage trip threshold
//Default is 0b.01.UVTRIP(0x97).0000 = 0x1970 = 6,512
//UnderVoltage = (UV_TRIP * GAIN) + ADCOFFSET
//Gain and Offset is different for each IC
//Example: voltage = (6,512 * 0.370) + 56mV = 2.465V
float readUVtrip(void)
{
  int trip = registerRead(bq796x0_UV_TRIP);

  trip <<= 4; //Shuffle the bits to align to 0b.01.XXXX.XXXX.0000
  trip |= 0x1000;

  float underVoltage = ((float)trip * gain) + offset;
  underVoltage /= 1000; //Convert to volts

  //Serial.print("underVoltage should be around 2.465: ");
  //Serial.println(underVoltage, 3);

  return(underVoltage);
}

//Given a voltage (2.85V for example), set the under voltage trip register
void writeUVtrip(float tripVoltage)
{
  byte val = tripCalculator(tripVoltage); //Convert voltage to an 8-bit middle value
  registerWrite(bq796x0_UV_TRIP, val); //address, value
}

//Under voltage and over voltage use the same rules for calculating the 8-bit value
//Given a voltage this function uses gain and offset to get a 14 bit value
//Then strips that value down to the middle-ish 8-bits
//No registers are written, that's up to the caller
byte tripCalculator(float tripVoltage)
{
  tripVoltage *= 1000; //Convert volts to mV

  //Serial.print("tripVoltage to be: ");
  //Serial.println(tripVoltage, 3);

  tripVoltage -= offset;
  tripVoltage /= gain;

  int tripValue = (int)tripVoltage; //We only want the integer - drop decimal portion.

  //Serial.print("tripValue should be something like 0x2BC0: ");
  //Serial.println(tripValue, HEX);

  tripValue >>= 4; //Cut off lower 4 bits
  tripValue &= 0x00FF; //Cut off higher bits

  //Serial.print("About to report tripValue: ");
  //Serial.println(tripValue, HEX);

  return(tripValue);
}

//Write a given value to a given register
void registerWrite(byte regAddress, byte regData)
{
  Wire.beginTransmission(bqI2CAddress);
  Wire.write(regAddress);
  Wire.endTransmission();

  Wire.beginTransmission(bqI2CAddress);
  Wire.write(regAddress);
  Wire.write(regData);
  Wire.endTransmission();
}

//Returns a given register
byte registerRead(byte regAddress)
{
  Wire.beginTransmission(bqI2CAddress);
  //Here's where I2C can time out
  Wire.write(regAddress);
  Wire.endTransmission();

  Wire.requestFrom(bqI2CAddress, 1);

  /*byte counter = 0;
  while(Wire.available() == 0)
  {
    delay(1);
    if(counter++ > 250) return(0); //Return with error
  }*/

  return(Wire.read());
}

//Returns the atmoic int from two sequentials reads
int registerDoubleRead(byte regAddress)
{
  Wire.beginTransmission(bqI2CAddress);
  Wire.write(regAddress);
  Wire.endTransmission();

  Wire.requestFrom(bqI2CAddress, 2);

  /*byte counter = 0;
   while(Wire.available() < 2)
   {
   Serial.print(".");
   if(counter++ > MAX_I2C_TIME)
   {
   return(-1); //Time out error
   }
   delay(1);
   }*/

  byte reg1 = Wire.read();
  byte reg2 = Wire.read();

  //Serial.print("reg1: 0x");
  //Serial.print(reg1, HEX);
  //Serial.print(" reg2: 0x");
  //Serial.println(reg2, HEX);

  int combined = (int)reg1 << 8;
  combined |= reg2;

  return(combined);
}

//Given a resistance on a super common 103AT-2 thermistor, return a temperature in C
//This is a poor way of converting the resistance to temp but it works for now
//From: http://www.rapidonline.com/pdf/61-0500e.pdf
int thermistorLookup(float resistance)
{
  //Resistance is coming in as Ohms, this lookup table assume kOhm
  resistance /= 1000; //Convert to kOhm

  int temp = 0;

  if(resistance > 329.5) temp = -50;
  if(resistance > 247.7) temp = -45;
  if(resistance > 188.5) temp = -40;
  if(resistance > 144.1) temp = -35;
  if(resistance > 111.3) temp = -30;
  if(resistance > 86.43) temp = -25;
  if(resistance > 67.77) temp = -20;
  if(resistance > 53.41) temp = -15;
  if(resistance > 42.47) temp = -10;
  if(resistance > 33.90) temp = -5;
  if(resistance > 27.28) temp = 0;
  if(resistance > 22.05) temp = 5;
  if(resistance > 17.96) temp = 10;
  if(resistance > 14.69) temp = 15;
  if(resistance > 12.09) temp = 20;
  if(resistance > 10.00) temp = 25;
  if(resistance > 8.313) temp = 30;

  return(temp);
}

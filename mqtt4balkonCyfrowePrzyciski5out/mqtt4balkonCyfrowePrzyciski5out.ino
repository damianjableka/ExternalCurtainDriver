#include <NewPingESP8266.h>
#include <WiFiClient.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <pcf8574_esp.h>
#include <string.h>

/*
 * Wersja wgrana do arduino i zamontowana 20181216 dziala prawidlowo
 * 
 * Chyba sie cos z prądem piepszy jak jest podlaczone do USB tak długo i sie relayamy pyka
 * 
 * skiepscil sie ping sonara wszystko zwiazane z zasilaniem, za zasilaczu 3A idzie jak buza 
 * 
 * 2018-12-12 wygląda na to ze bedzie gites wszystkie funkcjonalnosci dzialaja nalezy rozpoczac faze testow
 * 
 * Wersja 5 2018-12-09
 * Do zrobienia:
 * 
  *  // tego racze nie trzeba sie zobaczy Co z tymi zaokragleniami zeby nie bylo ze zostanie szpara potem ale na to rada jest dodanie nieco do czasu, kto wie moze nawet pewnego mnoznika typu 1.05 do czasu trwania aby byl rozpend tego ruchubo na pewno 20 x 1 s to nie jest tak samo zamkniety jak 20s na raz 
 * 
 * *
 * Lepsze zrozumienie procentow nadawanych przez mqtt:
 *    otworz 30 i zamknij 70 znaczy to samo, 
 *    najpierw sprawdzamy jaki jest aktualny procent
 *    porowonujemy z tym co przyszlo
 *    decydujemy czy otworz czy zamknij --> pin antypin
 *    dzialamy
 *    problem z tym jak jeest stan nieznany -1
 *    wtedy mozemy zawsze startowac jak relatywny czyli zakladamy ze ten ktory naciskamy jest 0
 *    Problem z tym ze jak 3x zrobmiy 40% zamknij to juz powinnismy wiedziec ze jest 100%
 *    w zwiazku z tym nalezaloby dodac do tablicy jeszcze jedna pozycje TmpProcent, ktory przechowuje wartosc procentow jesli jest  w glownym procencie -1
 *    Dopiero gdy TmpProcent przkroczy 100 to robimy update do procentu i juz wszystko gites dziala
 *    
 
 * 
 * Zrobione:
 * Zmierzyc czas zaykania i otwierania okna i drzwi bakonowych   
 * komenda przez mqtt ktora wywola oddanie wszystkich statusow. rolet/balkon/status/stany
 * while zamiast ifow w callback ale czy dziala?
 * procenty rozkminone na roznice ale w obu i zamknij i otworz
 *  dodany procentTMP aby trzymal male ruchy zeby moglo sie nazbierac do jedynki po restarcie.
 *  coz ty -1 zamienic to w informacji do mqtt na 0 jeśli  -1
 * Brak zrobionych w obecnje wersji:
 * 
 * Zrobione z poprzednich wersji:
 * 
 * Na dzien 2018 11 20 22:16 wydaje sie ze wszystko zwiazane z logika przyciskow i updatem procentow dziala poprawnie  nie było testów zadnej funkcjonalnosci MQTT
 * Do wszystkich innych wstawić nową unikatową nazwę mqttClientName to moze zatrzyma wywalanie klijenta po pewnym czasie
 * 
 * Wersja bez numeru działa elektroniczny przycisk zgodnie z oczekwianiem w logice krótkie i długie przycisniecie oraz przerwanie z czujnika odległosci
 * 
 * Zrobione
 * Wysyałeni do MQTT sygnalow zwrotnych w tym stanu
 * Aktualny Stan
 * Diody
 * Wylaczenie przez MQTT dzialania czujnika ruchu
 * 
 */


const char* ssid = "betelgeza";
const char* password =  "1qazxsw2";
const char* mqttServer = "192.168.0.4";
const int mqttPort =1883;
const char* mqttClientName="03balkon"; //01lazienka 02salon 03balkon 
const char* mqttUser = "";
const char* mqttPassword = "";
unsigned long CzasResetu=300000; //5 minut w milisekundach
unsigned long OstatniReset;
unsigned long SonarTimer;
unsigned long SonarDelay=50; //50 czas martwy dla sonara, nie mozna tego mierzyć zbyt szybko
const int SonarMin=70; // 70cm jesli cos znajdzie sie blizej to przerywa zamykanie balkonu
const int PinZamykaniaDrzwi=1; //informuje ktory pin test tym od zamykania drzwi ktory powinien sie zatrzymac jesli wykryjemy dystans
unsigned long CzasMartwyPrzyciskow=50; //ms czas martwy drzenia przycisku tak aby go prubkowac 20Hz
unsigned long KrotkiePrzycisniecie=800; //ms co to znaczy krotkie przycisniecie
const int IlePrzyciskow=4;// definiuje ilosc przyciskow
const char* mqttProcent="procent";
const char* mqttRuch="ruch";
double CzySonar=1; // zmienna pozwalajaca wylaczyc sonar przez mqtt typ double a nie bool bo używamy do przeczytania wartości tej samej co do procentów
const int IloscPinow=4;

#define TRIGGER_PIN  D6  // Arduino pin tied to trigger pin on the ultrasonic sensor.
#define ECHO_PIN     D5  // Arduino pin tied to echo pin on the ultrasonic sensor.
#define MAX_DISTANCE 200 // Maximum distance we want to ping for (in centimeters). Maximum sensor distance is rated at 400-500cm.

NewPingESP8266 sonar(TRIGGER_PIN, ECHO_PIN, MAX_DISTANCE); // NewPingESP8266 setup of pins and maximum distance.



WiFiClient espClient;

PubSubClient client(espClient);


//To tak nie zadziala bo przeca cyfrowe sa piney do 8 ale zastosujem PCF8574 i wszystko zagro
//zrobimy tu strukture do tych przycisków 

 struct PRZYCISK{
 int pin;
 int stan;
 unsigned long OstatniaZmiana;
 int relay;
 };
 
 PRZYCISK przyciski[IlePrzyciskow];
 
 TwoWire testWire;
  // Initialize a PCF8574 at I2C-address 0x20, using GPIO5, GPIO4 and testWire for the I2C-bus  
  PCF857x pcf8574(0x20, &testWire);
  
  // PCF
//  PCF P2 ( 6) --- 
//      P3 ( 7) --- 
//      P0 ( 4) ---
//      P1 ( 5) ---
//      P7 (12) ---
//      P6 (11) --- 
//      P5 (10) --- 
//      P4 ( 9) --- 
//     vcc (16) ---  +5V wemos czerowny
//     SDA (15) --- D2 SDA wemos zielony
//     SCL (14) --- D1 SCL wemos pomaranczowy
//     GND  (8) --- gnd
//      A0  (1) --- gnd
//      A1  (2) --- gnd
//      A2  (3) --- gnd  ---> adres 0x20
//


struct PIN{
  int numer;
  int antypin;
  int stan_stary;
  int stan;
  unsigned long czas_poczatku;
  unsigned long czas_trwania;
  String nazwa;
  String czynnosc;
  int czas_max;
  int PinDiody; //numer pinu na pcf najlepiej relacja relay - pin 0-4 1-5 2-6 3-7
  double procent;  // 0-1 a moze tez -1 aby byl stan nieustalony po resecie a dopiero po pelnym otworz albo zamknij byloby skalibrowane
  double procentTMP;  // 0-1 a moze tez -1 aby byl stan nieustalony po resecie a dopiero po pelnym otworz albo zamknij byloby skalibrowane
  };

PIN piny[IloscPinow];



void setup()
{
  Serial.begin(9600);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print("laczenie z siecia WiFi ");
    
  }
  
  Serial.println("polaczono z siecia WiFi ");
  Serial.println(ssid);
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

    
  client.setServer(mqttServer, mqttPort);
  
  client.setCallback(callback);
  
  while (!client.connected()) {
    Serial.println("Laczenie do serwera MQTT ");
  
    if(client.connect(mqttClientName, mqttUser, mqttPassword )) 
{
     Serial.println("polaczono");
    }else{
      Serial.print("failed state ");
      Serial.print(client.state());
      delay(2000);
    }
  }
  client.publish("rolety/status","balkon i drzwi zglasza sie!");
  client.subscribe("rolety/#");// here is where you later add a wildcard

OstatniReset=millis();
SonarTimer=millis();




int i;
for(i=0;i<IloscPinow;i++){
piny[i].stan_stary=HIGH; 
piny[i].stan=HIGH;
piny[i].czas_poczatku=millis();
piny[i].czas_trwania=0;
piny[i].procent=-1;
piny[i].procentTMP=0;  
  }
  
piny[0].numer=D3;//D1;
piny[0].antypin=1;//D2;
piny[0].nazwa="rolety/drzwi";
piny[0].czynnosc="otworz";
piny[0].czas_max=28000;
piny[0].PinDiody=4;
 
piny[1].numer=D4;//D2;
piny[1].antypin=0;//D1;
piny[1].nazwa="rolety/drzwi";
piny[1].czynnosc="zamknij";
piny[1].czas_max=27000;
piny[1].PinDiody=5;


piny[2].numer=D7;//D3;
piny[2].antypin=3;//D4;
piny[2].nazwa="rolety/balkon";
piny[2].czynnosc="otworz";
piny[2].czas_max=21000;
piny[2].PinDiody=6;

piny[3].numer=D0;//D4; // nie używać D8 do relaya bo sie sypie cały booting
piny[3].antypin=2;//D3;
piny[3].nazwa="rolety/balkon";
piny[3].czynnosc="zamknij";
piny[3].czas_max=19000;
piny[3].PinDiody=7;

  
  przyciski[0].pin=0;
  przyciski[0].stan=HIGH; //HIGH -> nie wcisienty, LOW-> wcisniety
  przyciski[0].OstatniaZmiana=millis();
  przyciski[0].relay=0;
  
  przyciski[1].pin=1;
  przyciski[1].stan=HIGH; //HIGH -> nie wcisienty, LOW-> wcisniety
  przyciski[1].OstatniaZmiana=millis();
  przyciski[1].relay=1;
  
  przyciski[2].pin=2;
  przyciski[2].stan=HIGH; //HIGH -> nie wcisienty, LOW-> wcisniety
  przyciski[2].OstatniaZmiana=millis();
  przyciski[2].relay=2;
  
  przyciski[3].pin=3;
  przyciski[3].stan=HIGH; //HIGH -> nie wcisienty, LOW-> wcisniety
  przyciski[3].OstatniaZmiana=millis();
  przyciski[3].relay=3;
 

    
 for(int i =0; i<IloscPinow;i++)
 {
   pinMode(piny[i].numer, OUTPUT);
   digitalWrite(piny[i].numer,HIGH);
  }
   
   testWire.begin();
   pcf8574.begin();
  
}

double wyznacz_procent(byte* payload, unsigned int length)
 {


  Serial.print("Message");
  String odp="";
  int k=1;
  for(int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
    odp+=(char)payload[i];
    if(isDigit((char)payload[i])){
      k=k*1;
      }
      else{
        k=k*0;
        }
  }
  
  if(k){
  Serial.println(((double)odp.toInt())/100);
  return(((double)odp.toInt())/100);
   }
   else
   {
    Serial.println("Podany parametr nie jest liczba przyjmuje 100%");
    return(1.);
   }
    Serial.println();
  
   
  
  }

void publikuj(String watek,String wiadomosc)  // publikuje String String zamienia je napierw na  char*[] char*[]
 {
      int dlugosc=watek.length();
      char ChWatek[dlugosc];
      watek.toCharArray(ChWatek,dlugosc);

      int dlugoscw=wiadomosc.length();
      char ChWiadomosc[dlugoscw];
      wiadomosc.toCharArray(ChWiadomosc,dlugoscw);
      
  client.publish(ChWatek,ChWiadomosc);
  }

void update_procent(int Relay, double DeltaProcent) //Relay to jest indeks w piny[Relay], DeltaProcent to tak naprawde liczba z zakresu [0-1]
  { //to musi dzialc tak ze zmienia dwa na raz (relay i antypin) w jednym dodaje w drugim odejmuje tak zeby zawsze zamkniete+otwarte dawalo 100%
    Serial.print("wyznacz procent");
    Serial.print(" ");
        Serial.print(Relay);
        Serial.print(" ");
        Serial.println(DeltaProcent);

    if(piny[Relay].procent!=-1){
      piny[Relay].procent=((piny[Relay].procent+DeltaProcent)>1?1:(piny[Relay].procent+DeltaProcent));
      piny[piny[Relay].antypin].procent=(1-piny[Relay].procent);
      
     }
      else{
       if(DeltaProcent>=1){
        piny[Relay].procent=1;   
        piny[piny[Relay].antypin].procent=0;   
        }
        else{
          piny[Relay].procentTMP+=DeltaProcent;
          piny[piny[Relay].antypin].procentTMP=(1-piny[Relay].procentTMP);
          
           if(piny[Relay].procentTMP>=1){
              piny[Relay].procent=1;   
              piny[piny[Relay].antypin].procent=0;   
             }
          }
      }
      
   
      Serial.println("opublikowano:");
      Serial.print(" ");
      Serial.print(Relay);
      Serial.println(" ");
      Serial.println(piny[Relay].procent);
      
      publikuj(piny[Relay].nazwa+"/"+piny[Relay].czynnosc+"/"+mqttProcent,String((piny[Relay].procent<0?0:piny[Relay].procent)*100));
      publikuj((piny[piny[Relay].antypin].nazwa+"/"+piny[piny[Relay].antypin].czynnosc+"/"+mqttProcent),String((piny[piny[Relay].antypin].procent<0?0:piny[piny[Relay].antypin].procent)*100));
   /*
    * sprawqdz czy jest -1
    * jesli jest -1 to sprawdz czy DeltaProcent jest 100 wtedy zapisujemy do odpowiednich
    * jesli nie jest -1 to odpowiednio dodajemy lub odejmujemy z tą uwagą ze jak suma przekroczy 100 to zatrzymujemy
    * 
    * robimy update przez mqtt do odpowiedniego Pin i antypin procent
    */
  }

void callback(char* topic, byte* payload, unsigned int length)
{
  Serial.print("Messageved in topic: ");
  Serial.println(topic);
  
  
  String temat=String(topic);
  double procentC=0;

  /*
   * Dodajemy topic w ktorym sprawdzamy wszystkie procenty po jego wywolaniu publikujemy w odpowiednich watkach
   * 
   */

   /*
   * Dodajemy topic w ktorym mozemy zmienic czy sonar ma dzialc czy nie 
   * Uwaga bo po resecie wroci do wartosci domyslnych z setupa, alejest ot jakies obejscie gdyby sonar sie zepsol
   * Ewentualnie mozna sie nauczyc jak to zapisac na stale do epromu???
   * 
   * 
   */

  if(temat.indexOf("sonar")!=-1)
   {
     Serial.println("Wykryto zmiane statusu sonara");
     CzySonar=wyznacz_procent(payload,length);
      client.publish("rolety/status","zmieniam sonar");
           //kiedys mozna tu dopisac taki dynks z odpowiedzia co bylo ostanio zrobione
   }


//robmimy taka komende ktora powoduje update stanow w mgtt 
// poblikujemy w rolety/drzwi/zamknij/procen
//wyorzystane mame procen i status
//   rolety/status/stany?
  

  if(temat.indexOf("status")!=-1)
   {
     Serial.println("Wykryto sprawdzenie statusu");
     if(temat.indexOf("zyjesz")!=-1)
      {
      client.publish("rolety/status","balkon i drzwi zyja i maja sie dobrze!");
      }
      //kiedys mozna tu dopisac taki dynks z odpowiedzia co bylo ostanio zrobione
      
       if(temat.indexOf("stany")!=-1)
       {
       for(int i=0;i<IloscPinow;i++)
          {
            publikuj(piny[i].nazwa+"/"+piny[i].czynnosc+"/"+mqttProcent,String((piny[i].procent<0?0:piny[i].procent)*100));
          }
       }
      
   }





//tutaj nalezalo by zrobic for po wszystkich zamiast takich kolejnych ifow
if(temat.indexOf(mqttRuch)!=-1||temat.indexOf("stop")!=-1){
   int i=0;
   while((temat.indexOf(piny[i].nazwa)==-1||!((temat.indexOf(piny[i].czynnosc)!=-1)||piny[i].czynnosc=="stop"))){ //&&(i<(IloscPinow+1))
    i++;
    Serial.println(i);
   }
          if(i<IloscPinow) //jesli cokolwiek spelnialo ale moglo tez nic nie spelniac 
          {
           Serial.print("zmiana: ");
                 Serial.println(i);
     if(piny[i].czynnosc=="stop")
        {
           Serial.print("STOP: ");
                 Serial.println(i);
            int aktywny=(piny[i].stan=LOW)?i:piny[i].antypin;
            procentC=((double)(millis()-piny[aktywny].czas_poczatku)/(double)piny[aktywny].czas_max);
            update_procent(aktywny,procentC);
            piny[piny[i].antypin].stan_stary=HIGH;
            piny[piny[i].antypin].stan=HIGH;
            piny[piny[i].antypin].czas_poczatku=0;
            piny[piny[i].antypin].czas_trwania=0;

            piny[i].stan_stary=HIGH;
            piny[i].stan=HIGH;
            piny[i].czas_poczatku=0;
            piny[i].czas_trwania=0;
        }
        else
         {
  /*     *    otworz 30 i zamknij 70 znaczy to samo, 
 *    najpierw sprawdzamy jaki jest aktualny procent
 *    porowonujemy z tym co przyszlo
 *    decydujemy czy otworz czy zamknij --> pin antypin
 *    dzialamy
 *    problem z tym jak jeest stan nieznany -1
 *    wtedy mozemy zawsze startowac jak relatywny czyli zakladamy ze ten ktory naciskamy jest 0
 *    Problem z tym ze jak 3x zrobmiy 40% zamknij to juz powinnismy wiedziec ze jest 100%
 *    w zwiazku z tym nalezaloby dodac do tablicy jeszcze jedna pozycje TmpProcent, ktory przechowuje wartosc procentow jesli jest  w glownym procencie -1
 *    Dopiero gdy TmpProcent przkroczy 100 to robimy update do procentu i juz wszystko gites dziala
 *    
        */      //przechodzimy z 30% zamkniecua na 60% zamkniecia to jst ok bo zamykamy
        //jak przechodzimy z 30% zamkniecia na 10 to musimy otwierac

          double akt_proc=(piny[i].procent<0?0:piny[i].procent);
          procentC=abs(akt_proc-wyznacz_procent(payload,length));
        // procent=abs(piny[i].procent-wyznacz_procent(payload,length));
            Serial.print("aktualny procent: ");
                 Serial.println(akt_proc);
          int ktory=(wyznacz_procent(payload,length)-akt_proc)>0?i:piny[i].antypin;
          Serial.print("ktory: ");
                 Serial.println(ktory);
              akt_proc=((piny[ktory].procent<0)?0:piny[ktory].procent);

              
               Serial.print("aktualny procent ktory: ");
                 Serial.println(akt_proc);
            piny[piny[ktory].antypin].stan_stary=HIGH;
            piny[piny[ktory].antypin].stan=HIGH;
            piny[piny[ktory].antypin].czas_poczatku=0;
            piny[piny[ktory].antypin].czas_trwania=0;

  
          piny[ktory].stan_stary=piny[ktory].stan;
          piny[ktory].stan=LOW;
          piny[ktory].czas_poczatku=millis();

          //tu nalezy to obmyslic
         //procent=(double)(wyznacz_procent(payload,length)-akt_proc);// to jest zawsze dodatnie przez zabieg z ktory
         Serial.print("procent: ");
                 Serial.println(procentC);
         Serial.print("piny[ktory].czas_max: ");
                 Serial.println(piny[ktory].czas_max);
                 
         piny[ktory].czas_trwania=piny[ktory].czas_max*procentC;//teraz trzeba zrobić to względnie do 
               Serial.print("czas: ");
                 Serial.println(piny[ktory].czas_trwania);
        // update_procent(ktory,procent); // w update_procent dorobic kawalek z takim procent_tmp
          
         }

       }

   }
 }

void zmieniaj()  // w tej funkcji tez musi bbyc zmiana aktualnego procentu tak aby bylo wiadomo jak sie zrobi ktrotkie przycisniecie i zostawi
  {
    unsigned long teraz=millis();
    
    for(int pin=0;pin<4;pin++)
    {
     if((teraz-piny[pin].czas_poczatku)<piny[pin].czas_trwania)  
      {
       if(piny[pin].stan!=piny[pin].stan_stary)
        {
         digitalWrite(piny[piny[pin].antypin].numer,HIGH);  
         digitalWrite(piny[pin].numer,LOW);  
         piny[pin].stan_stary=piny[pin].stan;
         
         pcf8574.write(piny[piny[pin].antypin].PinDiody,HIGH); //zgas diode antypinu
         pcf8574.write(piny[pin].PinDiody,LOW); //zapal diode pinu
         
        }
      }
      else
      {
       if(piny[pin].stan==LOW)
        {
          double procent=(double)(teraz-piny[pin].czas_poczatku)/(double)piny[pin].czas_max;
          update_procent(pin,procent);
        }
        
       digitalWrite(piny[pin].numer,HIGH);  
       piny[pin].stan=HIGH;
       piny[pin].stan_stary=HIGH;
       
       pcf8574.write(piny[pin].PinDiody,HIGH); //zgas diode pinu
      }
    }
   return;
   }


void loop()
{
  double procent=0;
  //sonar
  if(((millis()-SonarTimer)>SonarDelay)&&(piny[PinZamykaniaDrzwi].stan==LOW)&&CzySonar)//sprawdzamy wtedy i tylko wtedy gdy faktycznie sie zamyka a nie za kazdym razem te zmienne zmieniac
  {
    SonarTimer=millis();
 // delay(50);                     // Wait 50ms between pings (about 20 pings/sec). 29ms should be the shortest delay between pings.
  Serial.print("Ping: ");
  Serial.print(sonar.ping_cm()); // Send ping, get distance in cm and print result (0 = outside set distance range)
  Serial.println("cm");
  if(sonar.ping_cm()<SonarMin) //jesli za mala odleglosc to zatrzymuje zamykanie drzwi
   {
     Serial.println("przerywam");
      procent=((double)(millis()-piny[PinZamykaniaDrzwi].czas_poczatku)/(double)piny[PinZamykaniaDrzwi].czas_max);
      //procent=procent>1?1:procent;
      
      piny[PinZamykaniaDrzwi].stan_stary=HIGH;
      piny[PinZamykaniaDrzwi].stan=HIGH;
      piny[PinZamykaniaDrzwi].czas_poczatku=0;
      piny[PinZamykaniaDrzwi].czas_trwania=0;

      update_procent(PinZamykaniaDrzwi,procent);
       
      //sprawdzamy ile minelo od czas_poczatku  i )
    }
  }
  ///koniec sonaru
  
    //przyciski
    
    int knefel;
       for(knefel=0;knefel<IlePrzyciskow;knefel++ )
       {
        
       int StanTmp=pcf8574.read(przyciski[knefel].pin);  //czytamy stan z przycisku
       
    //   Serial.print(przyciski[knefel].pin); //debug
   //    Serial.print(" - ");
    //   Serial.println(StanTmp);
     
      if((przyciski[knefel].stan!=StanTmp)&&((millis()-przyciski[knefel].OstatniaZmiana)>CzasMartwyPrzyciskow)) //porownojemy z poprzednim stanem zapisanym w przycisku i z czasem od ostatniej zmiany stanu
        {
          if(przyciski[knefel].stan>StanTmp){ //jesli stan poprzedni był wiekszy niz aktualny czyli zmiana z High na LOW
            //rozumiemy przez to wcisniecie przycisku 
             przyciski[knefel].OstatniaZmiana=millis(); //zaraz po wcisnieciu wpisujemy kirdy ono nastapilo

Serial.print(przyciski[knefel].pin); //debug
       Serial.print(" - ");
      Serial.println(StanTmp);
             
            if(piny[przyciski[knefel].relay].stan&&piny[piny[przyciski[knefel].relay].antypin].stan){ //sprawdzamy czy oba sa na 1 czyli nie ma ruchu
              

               piny[przyciski[knefel].relay].stan_stary=piny[przyciski[knefel].relay].stan;
               piny[przyciski[knefel].relay].stan=LOW;
               piny[przyciski[knefel].relay].czas_poczatku=millis();   //!!!!!!!!!!!!!!!11  procent moze być -1 i wtedy zonk ponizej!!!!!!!!!!!!!!!!!!!!!!!!!!!!
               double TrueProcent=(piny[przyciski[knefel].relay].procent==-1)?0:piny[przyciski[knefel].relay].procent;
               piny[przyciski[knefel].relay].czas_trwania=piny[przyciski[knefel].relay].czas_max*(1.-TrueProcent);// czyli zamykamy z maksymalnym czasem
              //**************************   jesli dodamy aktualny procent zamkniecia to czas_trwania pownien być tylko czas_max*(1-aktualny procent)*******************************8/
             }
             else{ //jesli ktorykolwiek był wciesniety to po kliknieciu wylaczamy oba

               int aktywny=(piny[przyciski[knefel].relay].stan==LOW)?przyciski[knefel].relay:piny[przyciski[knefel].relay].antypin;
                
               procent=((double)(millis()-piny[aktywny].czas_poczatku)/(double)piny[aktywny].czas_max);
               Serial.println(millis());
               Serial.println(piny[aktywny].czas_poczatku);
              Serial.println(millis()-piny[aktywny].czas_poczatku);
                  Serial.println("double:");
               Serial.println((double)(millis()-piny[aktywny].czas_poczatku));
               Serial.println("czas_max:");
                 Serial.println((double)piny[aktywny].czas_max);
                   Serial.println(((double)(millis()-piny[aktywny].czas_poczatku)/(double)piny[aktywny].czas_max));
                 
                       
                        
              update_procent(aktywny,procent);
                
              
               piny[przyciski[knefel].relay].stan_stary=HIGH;
               piny[przyciski[knefel].relay].stan=HIGH;
               piny[przyciski[knefel].relay].czas_poczatku=0;
               piny[przyciski[knefel].relay].czas_trwania=0;
               
               piny[piny[przyciski[knefel].relay].antypin].stan_stary=HIGH;
               piny[piny[przyciski[knefel].relay].antypin].stan=HIGH;
               piny[piny[przyciski[knefel].relay].antypin].czas_poczatku=0;
               piny[piny[przyciski[knefel].relay].antypin].czas_trwania=0;

               
                   //sprawdzamy ktory byl wcisniety i obliczmy procent
                   //*************************tutaj tez by wypadalo policzyc ile trwalo dlugie przycisniecie i zaupdeattowac ten parametr*********************************8/
                }
               
              }
           if(przyciski[knefel].stan<StanTmp){ // stan aktualny jest wiekszy niz poprzedni czyli zmiana z LOw na High
               //rozumiemy przez to puszczenie przycisku
             if((millis()-przyciski[knefel].OstatniaZmiana>KrotkiePrzycisniecie)&&(piny[przyciski[knefel].relay].stan==LOW)){// jesli od przycisniecia do puszczenia minelo wiecej niz krotkie przycisniecie to zatrzymaj wszystko
               //jesli nie to nic nie rob: krotkie wcisnienie poprostu albo przrywa albo wlacza 100%
                //!!!!!!!!!!!!1 i stan był na LOW
                double procent=((double)(millis()-piny[przyciski[knefel].relay].czas_poczatku)/(double)piny[przyciski[knefel].relay].czas_max);
              
              update_procent(przyciski[knefel].relay,procent);
               
                piny[przyciski[knefel].relay].stan_stary=HIGH;
                piny[przyciski[knefel].relay].stan=HIGH;
                piny[przyciski[knefel].relay].czas_poczatku=0;
                piny[przyciski[knefel].relay].czas_trwania=0;
                 //*************************tutaj tez by wypadalo policzyc ile trwalo dlugie przycisniecie i zaupdeattowac ten parametr*********************************8/
                 
                }
                     
           }
        przyciski[knefel].stan=StanTmp;
       
        
        
          
        }
       }
     ///koniec przyciskow 
     
    //automatyczne ponowen polaczenie
     if((millis()-OstatniReset)>CzasResetu) //CzasResetu 5*60*1000 // 5 minut?
      { // kopia z setup
         OstatniReset=millis();
         int czy_reset_potrzebny=0;
         while (!client.connected()) {
         czy_reset_potrzebny=1;
         Serial.println("Laczenie do serwera MQTT ");
  
      if(client.connect(mqttClientName, mqttUser, mqttPassword )) 
      {
       Serial.println("polaczono");
      }else{
       Serial.print("failed state ");
       Serial.print(client.state());
       delay(2000);
      }
     }
     
  if(czy_reset_potrzebny)
  {
  client.subscribe("rolety/#");
   client.publish("rolety/status","balkon i drzwi zglasza sie po systemowym reconnekcie MQTT!");

  }
  else
   {
    client.publish("rolety/status","balkon i drzwi ciagle zyje");
   }
  
  }
  /// koniec automatycznego ponownego polaczenia
  
  client.loop();
  zmieniaj();
} 

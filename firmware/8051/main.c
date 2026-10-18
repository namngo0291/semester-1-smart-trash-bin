#include <reg51.h>

/* ================== KIEU DU LIEU CO BAN ================== */
#define uchar unsigned char
#define uint  unsigned int

/* ================== LCD1602 – 4-BIT MODE ================== */
#define LCD_DATAPINS   P0
sbit LCD_RS = P2^6;
sbit LCD_RW = P2^5;
sbit LCD_E  = P2^7;

/* ================== DS18B20 ================== */
sbit DSPORT = P3^7;

/* ================== HC-SR04 #1 (CUA) ================== */
sbit TRIG  = P2^0;
sbit ECHO  = P2^1;

/* ================== HC-SR04 #2 (THUNG RAC) ============= */
sbit TRIG2 = P2^2;
sbit ECHO2 = P2^3;

/* ================== CAM BIEN LUA ================== */
/* Module FLAME active LOW: co lua = 0 */
sbit FLAME = P3^2;

/* ================== NUT K4 – CHON MODE M/A ============= */
sbit K4 = P3^3;      // NEW: K4 nhan giu, active LOW

/* Nguong FULL cho thung – phai chinh theo thuc te
 * t = so vong lap echo; t cang nho -> rac cang gan cam bien.
 */
#define BIN_FULL_THRESHOLD   80   // tam: 80

/* ================== SERVO + AM THANH (ACTIVE LOW) ======== */
/* 1 = KHONG KICH, 0 = KICH RELAY / KICH AM THANH */
sbit SERVO       = P1^0;
sbit SOUND_OPEN  = P1^7;  // am thanh khi MO NAP
sbit SOUND_FULL  = P1^6;  // am thanh khi THUNG RAC FULL
sbit SOUND_ALARM = P1^5;  // am thanh khi CHAY (lua + nhiet do du nguong)

#define SERVO_MIN_MS   1   // xung cao ~1ms (dong)
#define SERVO_MAX_MS   2   // xung cao ~2ms (mo)

/* ================== BIEN TRANG THAI ===================== */
/* lid_open      = 0: nap dang dong, 1: nap dang mo
 * last_bin_full = ket qua FULL/OK duoc do lan CUOI KHI DO (luc dong nap)
 */
bit lid_open      = 0;
bit last_bin_full = 0;   // 0 = OK

/* NEW: mode M/A + servo manual */
bit mode_manual       = 1;   // 1 = MANUAL, 0 = AUTO
bit servo_manual_open = 0;   // trong manual: 1=mo, 0=dong

/* ================== PROTOTYPE ================== */
void LcdDelay1ms(uint c);
void LcdWriteCom(uchar com);
void LcdWriteData(uchar dat);
void LcdInit(void);

void Delay1ms(uint y);

unsigned char Ds18b20Init(void);
void Ds18b20WriteByte(unsigned char dat);
unsigned char Ds18b20ReadByte(void);
void Ds18b20Start(void);
void Ds18b20ReadCmd(void);
int  Ds18b20ReadTemp(void);   // don vi 1/16 °C

void Timer0_Init(void);
unsigned int HCSR04_GetDistance(void); // cam bien #1 – Timer0

/* Servo kieu cu dung Delay1ms */
void ServoPulse(uint high_ms);
void ServoSet(uint high_ms);

/* HC-SR04 #2 – khong dung Timer */
unsigned int HCSR04_BinMeasure(void);
bit BinIsFull(void);

/* Hien thi LCD */
void LCD_Show(unsigned int dist, int temp_raw, bit bin_full,
              bit fire, bit alarm_fire);
void LCD_ShowMode(bit manual);           // NEW

/* ===== UART ===== */
void UART_Init(void);
void UART_SendChar(char c);
void UART_SendUInt(uint val);
void UART_SendTemp_DS18B20(int temp_raw);
void UART_SendBinStatus(bit bin_full);
void UART_SendFireStatus(bit fire, bit alarm_fire);
void UART_ISR(void);       // NEW: ISR nhan lenh tu 8266


/* ========================================================= */
/* ================== LCD – DELAY & LENH =================== */
/* ========================================================= */

void LcdDelay1ms(uint c)
{
    uchar a,b;
    for(; c>0; c--)
        for(b=199; b>0; b--)
            for(a=1; a>0; a--);
}

void LcdWriteCom(uchar com)
{
    LCD_RS = 0;
    LCD_RW = 0;
    LCD_DATAPINS = com;      // high nibble -> P0.7..P0.4
    LcdDelay1ms(1);
    LCD_E = 1; LcdDelay1ms(5); LCD_E = 0;

    LCD_DATAPINS = com << 4; // low nibble
    LcdDelay1ms(1);
    LCD_E = 1; LcdDelay1ms(5); LCD_E = 0;
}

void LcdWriteData(uchar dat)
{
    LCD_RS = 1;
    LCD_RW = 0;
    LCD_DATAPINS = dat;
    LcdDelay1ms(1);
    LCD_E = 1; LcdDelay1ms(5); LCD_E = 0;

    LCD_DATAPINS = dat << 4;
    LcdDelay1ms(1);
    LCD_E = 1; LcdDelay1ms(5); LCD_E = 0;
}

void LcdInit()
{
    LcdWriteCom(0x32);  // ep ve 4-bit
    LcdWriteCom(0x28);  // 4-bit, 2 dong
    LcdWriteCom(0x0C);  // display on, cursor off
    LcdWriteCom(0x06);  // auto tang dia chi
    LcdWriteCom(0x01);  // clear
    LcdWriteCom(0x80);  // ve dau dong 1
}

/* ========================================================= */
/* ================== DELAY CHUNG 1ms ====================== */
/* ========================================================= */

void Delay1ms(uint y)
{
    uint x;
    for(; y>0; y--)
        for(x=110; x>0; x--);
}

/* ========================================================= */
/* ================== DS18B20 ============================== */
/* ========================================================= */

unsigned char Ds18b20Init()
{
    unsigned int i;
    DSPORT = 0;
    i = 70;
    while(i--);   // ~600us
    DSPORT = 1;

    i = 0;
    while(DSPORT)
    {
        i++;
        if(i > 5000) return 0; // >5ms khong thay presence
    }
    return 1;
}

void Ds18b20WriteByte(unsigned char dat)
{
    uint i, j;
    for(j=0; j<8; j++)
    {
        DSPORT = 0;
        i++;
        DSPORT = dat & 0x01;
        i = 6;
        while(i--);
        DSPORT = 1;
        dat >>= 1;
    }
}

unsigned char Ds18b20ReadByte()
{
    uchar byte=0, bi;
    uint i, j;

    for(j=8; j>0; j--)
    {
        DSPORT = 0;
        i++;
        DSPORT = 1;
        i++; i++;
        bi = DSPORT;
        byte = (byte>>1) | (bi<<7);
        i = 4;
        while(i--);
    }
    return byte;
}

void Ds18b20Start()
{
    Ds18b20Init();
    Delay1ms(1);
    Ds18b20WriteByte(0xCC);   // SKIP ROM
    Ds18b20WriteByte(0x44);   // CONVERT T
}

void Ds18b20ReadCmd()
{
    Ds18b20Init();
    Delay1ms(1);
    Ds18b20WriteByte(0xCC);   // SKIP ROM
    Ds18b20WriteByte(0xBE);   // READ SCRATCHPAD
}

/* Tra ve temp_raw: don vi 1/16 °C */
int Ds18b20ReadTemp()
{
    uchar tmh, tml;
    int temp;

    Ds18b20Start();
    Ds18b20ReadCmd();

    tml = Ds18b20ReadByte();
    tmh = Ds18b20ReadByte();

    temp = tmh;
    temp = (temp<<8) | tml;

    return temp;
}

/* ========================================================= */
/* ================== HC-SR04 #1 + TIMER0 ================== */
/* ========================================================= */

void Timer0_Init()
{
    TMOD &= 0xF0;
    TMOD |= 0x01;   // Timer0 mode 1
    TR0 = 0;
    TF0 = 0;
}

unsigned int HCSR04_GetDistance()
{
    uint count;

    // xung TRIG ~1ms
    TRIG = 0;
    Delay1ms(1);
    TRIG = 1;
    Delay1ms(1);
    TRIG = 0;

    // doi ECHO len
    while(!ECHO);
    TH0 = 0;
    TL0 = 0;
    TF0 = 0;
    TR0 = 1;

    // doi ECHO xuong
    while(ECHO);
    TR0 = 0;

    count = ((uint)TH0<<8) | TL0;
    return count / 58;  // gan dung cm
}

/* ========================================================= */
/* ================== HC-SR04 #2 – KHONG DUNG TIMER ======== */
/* ========================================================= */

/* Do “thoi gian” echo: so vong lap, khong phai cm */
unsigned int HCSR04_BinMeasure(void)
{
    unsigned int count;
    unsigned int timeout;

    /* Goi xung TRIG2 ~1ms */
    TRIG2 = 0;
    Delay1ms(1);
    TRIG2 = 1;
    Delay1ms(1);
    TRIG2 = 0;

    /* Doi ECHO2 len voi timeout tranh treo */
    timeout = 20000;   // giam timeout cho nhanh hon
    while (!ECHO2 && timeout--)
    {
        ;   // vong rong
    }
    if (timeout == 0)
    {
        // khong thay echo -> khong do duoc
        return 0;
    }

    /* Do do rong xung ECHO2 bang dem vong lap (khong dung timer) */
    count = 0;
    while (ECHO2 && count < 60000)
    {
        count++;
    }

    return count;  // don vi “vong lap”
}

/* Thung rac day khi rac gan cam bien => echo ngan => count nho
 * HAM NAY CHI DUOC GOI LUC DONG NAP.
 */
bit BinIsFull(void)
{
    unsigned int t;

    t = HCSR04_BinMeasure();

    if (t == 0)
    {
        // Khong do duoc (qua xa / loi) -> coi nhu chua day
        return 0;
    }

    if (t < BIN_FULL_THRESHOLD)
        return 1;   // FULL
    else
        return 0;   // OK
}

/* ========================================================= */
/* ================== SERVO (kieu cu dung Delay1ms) ========*/
/* ========================================================= */

/* 1 xung servo: high_ms ms HIGH + (20-high_ms) ms LOW */
void ServoPulse(uint high_ms)
{
    SERVO = 1;
    Delay1ms(high_ms);
    SERVO = 0;
    if (high_ms < 20)
        Delay1ms(20 - high_ms);
}

/* Giu servo o goc do ~40 xung (~800ms) */
void ServoSet(uint high_ms)
{
    uchar i;
    for(i = 0; i < 40; i++)
    {
        ServoPulse(high_ms);
    }
}

/* ========================================================= */
/* ================== UART 9600bps @ 11.0592MHz ============ */
/* ========================================================= */

void UART_Init(void)
{
    TMOD &= 0x0F;    // giu cau hinh Timer0, clear nibble Timer1
    TMOD |= 0x20;    // Timer1 mode 2 (8-bit auto-reload)

    TH1 = 0xFD;      // 9600 bps @ 11.0592MHz
    TL1 = 0xFD;

    TR1 = 1;         // chay Timer1

    SCON = 0x50;     // Mode1, 8-bit UART, REN=1

    ES  = 1;         // NEW: cho phep ngat UART
    EA  = 1;         // NEW: cho phep ngat toan cuc
}

/* Goi 1 ky tu */
void UART_SendChar(char c)
{
    SBUF = c;
    while (TI == 0);
    TI = 0;
}

/* Goi so nguyen duong dang ASCII (0..65535) */
void UART_SendUInt(uint val)
{
    char buf[5];
    char i = 0;

    if (val == 0)
    {
        UART_SendChar('0');
        return;
    }

    while (val > 0)
    {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }

    while (i--)
    {
        UART_SendChar(buf[i]);
    }
}

/* Goi nhiet do DS18B20: T=+25.3\r\n */
void UART_SendTemp_DS18B20(int temp_raw)
{
    int t10;
    char sign = '+';

    if (temp_raw < 0)
    {
        sign = '-';
        temp_raw = -temp_raw;
    }

    // t10 = °C * 10 = temp_raw * 10 / 16 (co lam tron)
    t10 = (temp_raw * 10 + 8) / 16;

    {
        uint int_part = t10 / 10;
        uint frac     = t10 % 10;

        UART_SendChar('T');
        UART_SendChar('=');
        UART_SendChar(sign);
        UART_SendUInt(int_part);
        UART_SendChar('.');
        UART_SendChar('0' + frac);
        UART_SendChar('\r');
        UART_SendChar('\n');
    }
}

/* Goi trang thai thung rac: BIN=FULL hoac BIN=OK */
void UART_SendBinStatus(bit bin_full)
{
    UART_SendChar('B');
    UART_SendChar('I');
    UART_SendChar('N');
    UART_SendChar('=');

    if (bin_full)
    {
        UART_SendChar('F');
        UART_SendChar('U');
        UART_SendChar('L');
        UART_SendChar('L');
    }
    else
    {
        UART_SendChar('O');
        UART_SendChar('K');
    }

    UART_SendChar('\r');
    UART_SendChar('\n');
}

/* Goi trang thai lua/chay: FIRE=0/1, ALARM=0/1 */
void UART_SendFireStatus(bit fire, bit alarm_fire)
{
    // FIRE=0/1
    UART_SendChar('F');
    UART_SendChar('I');
    UART_SendChar('R');
    UART_SendChar('E');
    UART_SendChar('=');
    UART_SendChar(fire ? '1' : '0');
    UART_SendChar('\r');
    UART_SendChar('\n');

    // ALARM=0/1
    UART_SendChar('A');
    UART_SendChar('L');
    UART_SendChar('A');
    UART_SendChar('R');
    UART_SendChar('M');
    UART_SendChar('=');
    UART_SendChar(alarm_fire ? '1' : '0');
    UART_SendChar('\r');
    UART_SendChar('\n');
}

/* ========================================================= */
/* =========== UART ISR – NH?N L?NH T? ESP8266 ============= */
/*  Giao th?c: "$O\n" -> m? n?p, "$C\n" -> dóng n?p        */
/* ========================================================= */

void UART_ISR(void) interrupt 4
{
    static unsigned char state = 0;
    unsigned char c;

    if (RI) {
        RI = 0;
        c = SBUF;

        if (state == 0) {
            if (c == '$') state = 1;  // b?t d?u khung l?nh
        } else if (state == 1) {
            if (c == 'O') {
                servo_manual_open = 1;   // Manual: m?
            } else if (c == 'C') {
                servo_manual_open = 0;   // Manual: dóng
            }
            state = 0;                   // b? qua '\n'
        }
    }
}

/* ========================================================= */
/* ================== HIEN THI LCD ========================= */
/* ========================================================= */

void LCD_Show(unsigned int dist, int temp_raw, bit bin_full,
              bit fire, bit alarm_fire)
{
    int t10;
    char sign = '+';

    if (temp_raw < 0)
    {
        sign = '-';
        temp_raw = -temp_raw;
    }

    // t10 = °C * 10 = temp_raw * 10 / 16 (integer)
    t10 = (temp_raw * 10 + 8) / 16;

    if (dist > 999) dist = 999;

    /* Dong 1: Dist: xxxcm OK/FULL (13–14 ký t? d?u) */
    LcdWriteCom(0x80);
    LcdWriteData('D'); LcdWriteData('i'); LcdWriteData('s'); LcdWriteData('t');
    LcdWriteData(':'); LcdWriteData(' ');

    LcdWriteData('0' + (dist/100)%10);
    LcdWriteData('0' + (dist/10)%10);
    LcdWriteData('0' + (dist%10));
    LcdWriteData('c'); LcdWriteData('m');
    LcdWriteData(' ');

    if (bin_full)
    {
        LcdWriteData('F');
        LcdWriteData('U');
        LcdWriteData('L');
        LcdWriteData('L');
    }
    else
    {
        LcdWriteData('O');
        LcdWriteData('K');
        LcdWriteData(' ');
        LcdWriteData(' ');
    }

    /* ==== DONG 2: UU TIEN BAO CHAY / LUA ==== */
    if (alarm_fire)   // Co lua + T > 33°C
    {
        LcdWriteCom(0xC0);
        LcdWriteData('B'); LcdWriteData('A'); LcdWriteData('O'); LcdWriteData(' ');
        LcdWriteData('C'); LcdWriteData('H'); LcdWriteData('A'); LcdWriteData('Y');
        LcdWriteData('!'); LcdWriteData('!');
        LcdWriteData(' '); LcdWriteData(' '); LcdWriteData(' '); LcdWriteData(' ');
        return;
    }
    else if (fire)    // Chi co lua, chua du nong
    {
        LcdWriteCom(0xC0);
        LcdWriteData('P'); LcdWriteData('H'); LcdWriteData('A'); LcdWriteData('T');
        LcdWriteData(' '); LcdWriteData('H'); LcdWriteData('I'); LcdWriteData('E');
        LcdWriteData('N'); LcdWriteData(' ');
        LcdWriteData('L'); LcdWriteData('U');
        LcdWriteData('A'); LcdWriteData(' '); LcdWriteData(' '); LcdWriteData(' ');
        return;
    }

    /* Neu khong co lua -> hien nhiet do */
    LcdWriteCom(0xC0);
    LcdWriteData('T'); LcdWriteData('e'); LcdWriteData('m'); LcdWriteData('p');
    LcdWriteData(':'); LcdWriteData(' ');

    LcdWriteData(sign);

    {
        int int_part = t10 / 10;   // phan nguyen °C
        int frac     = t10 % 10;   // phan le 0.1°C

        if (int_part >= 100) {
            LcdWriteData('0' + (int_part / 100));
            LcdWriteData('0' + ((int_part / 10) % 10));
            LcdWriteData('0' + (int_part % 10));
        } else if (int_part >= 10) {
            LcdWriteData('0' + (int_part / 10));
            LcdWriteData('0' + (int_part % 10));
        } else {
            LcdWriteData(' ');
            LcdWriteData('0' + int_part);
        }

        LcdWriteData('.');
        LcdWriteData('0' + frac);
        LcdWriteData('C');

        // Fill du 16 ky tu dong 2
        LcdWriteData(' ');
        LcdWriteData(' ');
        LcdWriteData(' ');
    }
}

/* NEW: In ch? M/A ? cu?i dòng 1 */
void LCD_ShowMode(bit manual)
{
    LcdWriteCom(0x8F);      // ô cu?i dòng 1 (addr 0x80 + 15)
    LcdWriteData(manual ? 'M' : 'A');
}

/* ========================================================= */
/* ================== MAIN ================================ */
/* ========================================================= */

void main()
{
    unsigned int dist;
    int temp_raw = 0;
    bit bin_now;

    /* Bien cam bien lua */
    bit fire = 0;
    bit alarm_fire = 0;

    Timer0_Init();
    LcdInit();
    UART_Init();   // UART cho ESP8266 / ESP32

    // Mac dinh tat ca am thanh: active LOW -> 1 = khong kich
    SOUND_OPEN  = 1;
    SOUND_FULL  = 1;
    SOUND_ALARM = 1;

    /* NEW: Reset servo dóng l?i khi b?t ngu?n */
    ServoSet(SERVO_MIN_MS);
    lid_open = 0;

    /* NEW: Mode kh?i d?ng = MANUAL */
    mode_manual       = 1;
    servo_manual_open = 0;

    while(1)
    {
        /* ===== NUT K4 – CHUYEN M/A (toggle) ===== */
        if (K4 == 0) {              // nhan (active LOW)
            Delay1ms(20);           // debounce
            if (K4 == 0) {
                mode_manual = !mode_manual;   // d?i M <-> A
                while (K4 == 0);   // cho nha tay ra
            }
        }

        /* ===== 1. LUA + NHIET DO (LUON DOC) ===== */

        /* Cam bien lua active LOW: co lua -> FLAME = 0 */
        fire = (FLAME == 0) ? 1 : 0;

        /* Doc nhiet do DS18B20 */
        temp_raw = Ds18b20ReadTemp();   // don vi 1/16 °C

        /* 33°C -> 33*16 = 528: nguong CHAY */
        if (fire && (temp_raw >= 33 * 16))
            alarm_fire = 1;   // CHAY
        else
            alarm_fire = 0;   // chi co lua thuong hoac khong lua

        /* ===== 2. LUON DO KHOANG CACH HC-SR04 (CUA) ===== */
        dist = HCSR04_GetDistance();

        /* --------- 3. LOGIC MO / DONG NAP + AM THANH ---------- */

        /* 3.1 – CO CHAY THAT SU (LUA + NHIET DO DU NGUONG)
         * CHI AUTO MO NAP KHI ? CH? Ð? AUTO.
         */
        if (!mode_manual && alarm_fire)
        {
            if (!lid_open)
            {
                ServoSet(SERVO_MAX_MS);
                lid_open = 1;
            }

            SOUND_ALARM = 0;
            Delay1ms(2000);
            SOUND_ALARM = 1;
        }
        else
        {
            if (mode_manual)
            {
                /* ===== MANUAL MODE =====
                 * Servo CH? theo l?nh t? 8266: servo_manual_open
                 * Không xét kho?ng cách / FULL d? m?–dóng.
                 */

                if (servo_manual_open && !lid_open)
                {
                    ServoSet(SERVO_MAX_MS);
                    lid_open = 1;
                }
                else if (!servo_manual_open && lid_open)
                {
                    ServoSet(SERVO_MIN_MS);
                    lid_open = 0;
                }
                /* Không do FULL / không hú SOUND_FULL ? dây */
            }
            else
            {
                /* ===== AUTO MODE – gi? nguyên logic cu ===== */

                /* Mo nap khi co nguoi truoc cam bien (chi khi KHONG co chay) */
                if (!lid_open && (dist > 0) && (dist < 20))
                {
                    ServoSet(SERVO_MAX_MS);
                    lid_open = 1;

                    /* Am thanh mo nap (P1.7) ~2s */
                    SOUND_OPEN = 0;
                    Delay1ms(2000);
                    SOUND_OPEN = 1;
                }
                /* Dong nap khi KHONG CO NGUOI (va khong co chay) */
                else if (lid_open && (dist == 0 || dist >= 25))
                {
                    ServoSet(SERVO_MIN_MS);
                    lid_open = 0;

                    /* NGAY LUC DONG NAP: do FULL 1 LAN */
                    bin_now = BinIsFull();
                    last_bin_full = bin_now;   // LUU KET QUA

                    /* Bao trang thai thung qua UART NGAY LUC NAY */
                    UART_SendBinStatus(last_bin_full);

                    /* Neu FULL thi keu SOUND_FULL 1 lan ~1s */
                    if (last_bin_full)
                    {
                        SOUND_FULL = 0;
                        Delay1ms(1000);
                        SOUND_FULL = 1;
                    }
                }
            }
        }

        /* ===== 4. HIEN THI LCD + GUI UART ===== */
        LCD_Show(dist, temp_raw, last_bin_full, fire, alarm_fire);
        LCD_ShowMode(mode_manual);          // NEW: in M/A

        // Gui nhiet do + trang thai lua/chay cho 8266 / ESP32
        UART_SendTemp_DS18B20(temp_raw);         // T=...
        UART_SendFireStatus(fire, alarm_fire);   // FIRE=..., ALARM=...

        // Giam spam UART
        Delay1ms(500);
    }
}

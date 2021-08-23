#include "embARC.h"
#include "embARC_debug.h"
#include "iic1602lcd.h"
#include <stdlib.h>
#include <time.h>

#define GPIO4B2_0_OFFSET 0
#define GPIO4B2_1_OFFSET 1
#define GPIO4B2_2_OFFSET 2
#define GPIO4B2_3_OFFSET 3
#define GPIO8B2_0_OFFSET 0
#define GPIO8B2_1_OFFSET 1
#define GPIO8B2_2_OFFSET 2
#define GPIO8B2_3_OFFSET 3
#define GPIO8B2_4_OFFSET 4
#define GPIO8B2_5_OFFSET 5
#define GPIO8B2_6_OFFSET 6
#define GPIO8B2_7_OFFSET 7
#define GPIO8B3_0_OFFSET 0
#define GPIO8B3_1_OFFSET 1
#define GPIO8B3_2_OFFSET 2
#define GPIO8B3_3_OFFSET 3
#define GPIO8B3_4_OFFSET 4
#define GPIO8B3_5_OFFSET 5
#define GPIO8B3_6_OFFSET 6
#define GPIO8B3_7_OFFSET 7
#define GPIO4B1_0_OFFSET 0
#define GPIO4B1_1_OFFSET 1

pLCD_t lcd_obj;

volatile bool ifint = false;  //用這個flag檢查是否曾進過interrupt
int count_down = 0;           //用來當做秒數
uint32_t chance = 0;          //用來儲存總共可以猜幾次
uint32_t mode_state[2] = {0}; //看現在是什麼模式

uint32_t Input(DEV_GPIO_PTR gpio_4b1, DEV_GPIO_PTR gpio_4b2, DEV_GPIO_PTR gpio_8b2, DEV_GPIO_PTR gpio_8b3);
int checkint();         //檢查ifint並把count_down減一
void t1_isr(void *ptr); //Timer 1的ISR

int main(void)
{

    /* get gpio object */
    DEV_GPIO_PTR gpio_4b1 = gpio_get_dev(DFSS_GPIO_4B1_ID);
    DEV_GPIO_PTR gpio_4b2 = gpio_get_dev(DFSS_GPIO_4B2_ID);
    DEV_GPIO_PTR gpio_8b2 = gpio_get_dev(DFSS_GPIO_8B2_ID);
    DEV_GPIO_PTR gpio_8b3 = gpio_get_dev(DFSS_GPIO_8B3_ID);

    lcd_obj = LCD_Init(DFSS_IIC_0_ID); //初始化IIC 0

    uint32_t start = 0;     //儲存control button的狀態(start)
    uint32_t restart = 0;   //儲存control button的狀態(restart)
    uint32_t ans = 0;       //儲存答案
    uint32_t guess = 0;     //儲存user猜了多少
    uint32_t mode[2] = {0}; //儲存mode button的狀態 mode[0] = Normal Mode mode[1] = Timer Mode

    /* 
        Open GPIO
        input   : 0, 2, 4, 6
        output  : 1, 3, 5, 7
    */

    gpio_4b1->gpio_open(1 << GPIO4B1_1_OFFSET);
    gpio_4b2->gpio_open(1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
    gpio_8b2->gpio_open(1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
    gpio_8b3->gpio_open(1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

    while (1)
    {

        uint32_t upper = 1023; //儲存目前答案範圍的最大值
        uint32_t lower = 0;    //儲存目前答案範圍的最小值
        chance = 10;           //初始化猜測的次數
        ifint = false;         //初始化
        mode_state[0] = 0;     //初始化
        mode_state[1] = 0;     //初始化

        srand(time(NULL)); //產生亂數
        ans = abs((rand() * 121)) % 1024;
        EMBARC_PRINTF("%d\n", ans); //將答案印到PuTTY上

        lcd_obj->clear();
        lcd_obj->blink_LED(OFF);
        lcd_obj->set_Color(WHITE);
        lcd_obj->printf("Select Mode:");
        board_delay_ms(1500, 0);
        lcd_obj->clear();
        lcd_obj->printf("9 ~ Normal Mode"); //button 9是Normal Mode
        lcd_obj->set_CursorPos(0, 1);
        lcd_obj->printf("8 ~ Timer Mode"); //button 8是Timer Mode

        while (1) //將整個主要運行部分用迴圈包起來以利實作restart
        {

            gpio_4b2->gpio_read(&mode[0], 1 << GPIO4B2_2_OFFSET);
            gpio_8b2->gpio_read(&mode[1], 1 << GPIO8B2_0_OFFSET);
            if ((mode[0] >> 2 == 1) && (mode[1] >> 0 == 0)) //檢查Normal Mode button是否有被按 且不能兩個同時按
            {
                gpio_4b2->gpio_write((mode[0] >> 2) << GPIO4B2_3_OFFSET, 1 << GPIO4B2_3_OFFSET); //若有按則對應的燈亮
                mode_state[0] = 1;                                                               //現在是Normal Mode
                break;
            }
            else if ((mode[0] >> 2 == 0) && (mode[1] >> 0 == 1)) //檢查Timer Mode button是否有被按 且不能兩個同時按
            {
                gpio_8b2->gpio_write(mode[1] << GPIO8B2_1_OFFSET, 1 << GPIO8B2_1_OFFSET); //若有按則對應的燈亮
                mode_state[1] = 1;                                                        //現在是Timer Mode
                break;
            }
        }

        while (((mode[0] >> 2) || (mode[1] >> 0)) == 1) //若按著Control button不放會卡在這個迴圈裡面
        {
            gpio_4b2->gpio_read(&mode[0], 1 << GPIO4B2_2_OFFSET);
            gpio_8b2->gpio_read(&mode[1], 1 << GPIO8B2_0_OFFSET);
        }

        lcd_obj->clear();
        lcd_obj->printf("Press button to");
        lcd_obj->set_CursorPos(0, 1);
        lcd_obj->printf("Start.");

        while (1) //檢查Control button是否有被按
        {
            gpio_4b2->gpio_read(&start, 1 << GPIO4B2_0_OFFSET);
            if (start >> 0 == 1)
            {
                gpio_4b2->gpio_write(1 << GPIO4B2_1_OFFSET, 1 << GPIO4B2_1_OFFSET); //有按則對應的燈亮 跳出迴圈
                break;
            }
        }

        lcd_obj->clear();

        if (mode_state[1] == 1) //若現在是Timer Mode則將秒數設為180秒 並啟動Timer1
        {
            count_down = 180; //秒數

            if (timer_present(TIMER_1))
            {
                // Stop TIMER1 & Disable its interrupt first
                timer_stop(TIMER_1);
                int_disable(INTNO_TIMER1);

                // Connect a ISR to TIMER1's interrupt
                int_handler_install(INTNO_TIMER1, t1_isr);

                // Enable TIMER1's interrupt
                int_enable(INTNO_TIMER1);

                // Start counting, 1 second request an interrupt
                timer_start(TIMER_1, TIMER_CTRL_IE, BOARD_CPU_CLOCK);
            }

            lcd_obj->set_CursorPos(13, 1); //印出現在的秒數
            lcd_obj->printf("%3d", count_down);
            lcd_obj->home();
        }

        lcd_obj->printf("%-4d<= X <= %-4d", lower, upper); //印出初始範圍
        lcd_obj->set_CursorPos(6, 1);
        lcd_obj->printf("%2d", chance); //印出總共可以猜幾次

        while (start >> 0 == 1) //若按著Control button不放會卡在迴圈裡面
        {
            gpio_4b2->gpio_read(&start, 1 << GPIO4B2_0_OFFSET);
            if (checkint() == 0) //若count_down == 0則跳出迴圈
                break;
        }

        guess = Input(gpio_4b1, gpio_4b2, gpio_8b2, gpio_8b3); //開始猜並將結果傳回guess

        while (1)
        {
            if ((mode_state[1] == 1) && (count_down == 0)) //若秒數等於0顯示You Lose!!
            {
                lcd_obj->clear();
                lcd_obj->blink_LED(OFF);
                lcd_obj->printf("You Lose!!");

                gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                board_delay_ms(1500, 0);

                lcd_obj->clear(); //印出Press button to restart.並跳出迴圈等待restart
                lcd_obj->set_Color(WHITE);
                lcd_obj->printf("Press button to");
                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("restart.");

                break;
            }
            else if (guess == ans) //若猜中答案顯示Congratulations!
            {
                lcd_obj->clear();
                lcd_obj->blink_LED(OFF);
                lcd_obj->set_Color(GREEN);
                lcd_obj->printf("Congratulations!");

                gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                board_delay_ms(1500, 0);

                lcd_obj->clear(); //印出Press button to restart.並跳出迴圈等待restart
                lcd_obj->set_Color(WHITE);
                lcd_obj->printf("Press button to");
                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("restart.");

                break;
            }
            else if (guess > ans && guess <= upper) //若猜測的值大於答案且小於等於上界
            {
                upper = guess - 1; //將上界更新為猜測的值並減一以正確顯示範圍
                lcd_obj->home();

                if (checkint() == 0) //若秒數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }

                if ((guess - ans < 50)) //若猜測的值比答案大不到50的話讓LCD紅色閃爍
                {
                    lcd_obj->set_Color(RED);
                    lcd_obj->blink_LED(ON);
                }
                lcd_obj->printf("%-4d<= X <= %-4d", lower, upper); //印出上下界的值並使用%-4d預留好位置並向左對齊
                board_delay_ms(200, 0);
                if (chance == 0) //若剩餘猜測次數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }
                gpio_4b2->gpio_write(1 << GPIO4B2_1_OFFSET, 1 << GPIO4B2_1_OFFSET); //要進入Input前先讓Control的燈亮表示enable write
                guess = Input(gpio_4b1, gpio_4b2, gpio_8b2, gpio_8b3);
            }
            else if (guess < ans && guess >= lower) //若猜測的值小於答案且大於等於上界
            {
                lower = guess + 1; //將下界更新為猜測的值並加一以正確顯示範圍
                lcd_obj->home();

                if (checkint() == 0) //若秒數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }

                if ((ans - guess < 50)) //若猜測的值比答案小不到50的話讓LCD紅色閃爍
                {
                    lcd_obj->set_Color(RED);
                    lcd_obj->blink_LED(ON);
                }
                lcd_obj->printf("%-4d<= X <= %-4d", lower, upper); //印出上下界的值並使用%-4d預留好位置並向左對齊
                board_delay_ms(200, 0);
                if (chance == 0) //若剩餘猜測次數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }
                gpio_4b2->gpio_write(1 << GPIO4B2_1_OFFSET, 1 << GPIO4B2_1_OFFSET); //要進入Input前先讓Control的燈亮表示enable write
                guess = Input(gpio_4b1, gpio_4b2, gpio_8b2, gpio_8b3);
            }
            else
            {
                lcd_obj->home();

                if (checkint() == 0) //若秒數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }

                lcd_obj->printf("Out of Range!!  ");
                board_delay_ms(1000, 0);
                if (chance == 0) //若剩餘猜測次數為0跳出迴圈等待restart
                {
                    lcd_obj->clear();
                    lcd_obj->blink_LED(OFF);
                    lcd_obj->printf("You Lose!!");

                    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
                    gpio_4b2->gpio_write(0, 1 << GPIO4B2_1_OFFSET | 1 << GPIO4B2_3_OFFSET);
                    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
                    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

                    board_delay_ms(1500, 0);

                    lcd_obj->clear();
                    lcd_obj->set_Color(WHITE);
                    lcd_obj->printf("Press button to");
                    lcd_obj->set_CursorPos(0, 1);
                    lcd_obj->printf("restart.");

                    break;
                }
                lcd_obj->home();
                lcd_obj->printf("%-4d<= X <= %-4d", lower, upper);
                board_delay_ms(200, 0);
                gpio_4b2->gpio_write(1 << GPIO4B2_1_OFFSET, 1 << GPIO4B2_1_OFFSET);
                guess = Input(gpio_4b1, gpio_4b2, gpio_8b2, gpio_8b3);
            }
        }

        while (1) //若按下Control button則重新開始遊戲
        {
            gpio_4b2->gpio_read(&restart, 1 << GPIO4B2_0_OFFSET);
            if (restart >> 0 == 1)
                break;
        }
    }

    gpio_4b1->gpio_close();
    gpio_4b2->gpio_close();
    gpio_8b2->gpio_close();
    gpio_8b3->gpio_close();

    return E_SYS;
}

uint32_t Input(DEV_GPIO_PTR gpio_4b1, DEV_GPIO_PTR gpio_4b2, DEV_GPIO_PTR gpio_8b2, DEV_GPIO_PTR gpio_8b3)
{
    uint32_t press = 0;           //儲存Control button的狀態
    uint32_t guess = 0;           //儲存猜的答案
    uint32_t power[10] = {0};     //儲存0 ~ 9按鈕的狀態
    uint32_t LED_State[10] = {0}; //儲存0 ~ 9LED的狀態

    gpio_4b1->gpio_write(0, 1 << GPIO4B1_1_OFFSET);
    gpio_4b2->gpio_write(0, 1 << GPIO4B2_3_OFFSET);
    gpio_8b2->gpio_write(0, 1 << GPIO8B2_1_OFFSET | 1 << GPIO8B2_3_OFFSET | 1 << GPIO8B2_5_OFFSET | 1 << GPIO8B2_7_OFFSET);
    gpio_8b3->gpio_write(0, 1 << GPIO8B3_1_OFFSET | 1 << GPIO8B3_3_OFFSET | 1 << GPIO8B3_5_OFFSET | 1 << GPIO8B3_7_OFFSET);

    gpio_4b2->gpio_read(&press, 1 << GPIO4B2_0_OFFSET);

    if (checkint() == 0) //隨時檢查若checkint()為0則回傳秒數
        return count_down;

    while (press != 1) //若沒有按下Control button則持續讓user改變欲猜測的答案
    {

        if (checkint() == 0)
            return count_down;

        gpio_4b2->gpio_read(&power[9], 1 << GPIO4B2_2_OFFSET); //讀取按鈕狀態
        if (power[9] >> 2 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[9] == 0) //若按的時候LED沒有亮
            {
                LED_State[9] = !LED_State[9];                                                  //將LED狀態轉為暗
                gpio_4b2->gpio_write(LED_State[9] << GPIO4B2_3_OFFSET, 1 << GPIO4B2_3_OFFSET); //將LED狀態的值寫入
                guess = guess + 512;                                                           //猜測的值增加那個button代表的值

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess); //使用%-4d來預留位置並向左對齊
                while (power[9] >> 2 == 1)
                {
                    gpio_4b2->gpio_read(&power[9], 1 << GPIO4B2_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else //若按的時候LED有亮
            {
                LED_State[9] = !LED_State[9];                                                  //將LED狀態轉為亮
                gpio_4b2->gpio_write(LED_State[9] << GPIO4B2_3_OFFSET, 1 << GPIO4B2_3_OFFSET); //將LED狀態的值寫入
                guess = guess - 512;                                                           //猜測的值減少那個button代表的值

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[9] >> 2 == 1)
                {
                    gpio_4b2->gpio_read(&power[9], 1 << GPIO4B2_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b2->gpio_read(&power[8], 1 << GPIO8B2_0_OFFSET);
        if (power[8] >> 0 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[8] == 0)
            {
                LED_State[8] = !LED_State[8];
                gpio_8b2->gpio_write(LED_State[8] << GPIO8B2_1_OFFSET, 1 << GPIO8B2_1_OFFSET);
                guess = guess + 256;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[8] >> 0 == 1)
                {
                    gpio_8b2->gpio_read(&power[8], 1 << GPIO8B2_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[8] = !LED_State[8];
                gpio_8b2->gpio_write(LED_State[8] << GPIO8B2_1_OFFSET, 1 << GPIO8B2_1_OFFSET);
                guess = guess - 256;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[8] >> 0 == 1)
                {
                    gpio_8b2->gpio_read(&power[8], 1 << GPIO8B2_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b2->gpio_read(&power[7], 1 << GPIO8B2_2_OFFSET);
        if (power[7] >> 2 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[7] == 0)
            {
                LED_State[7] = !LED_State[7];
                gpio_8b2->gpio_write(LED_State[7] << GPIO8B2_3_OFFSET, 1 << GPIO8B2_3_OFFSET);
                guess = guess + 128;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[7] >> 2 == 1)
                {
                    gpio_8b2->gpio_read(&power[7], 1 << GPIO8B2_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[7] = !LED_State[7];
                gpio_8b2->gpio_write(LED_State[7] << GPIO8B2_3_OFFSET, 1 << GPIO8B2_3_OFFSET);
                guess = guess - 128;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[7] >> 2 == 1)
                {
                    gpio_8b2->gpio_read(&power[7], 1 << GPIO8B2_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b2->gpio_read(&power[6], 1 << GPIO8B2_4_OFFSET);
        if (power[6] >> 4 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[6] == 0)
            {
                LED_State[6] = !LED_State[6];
                gpio_8b2->gpio_write(LED_State[6] << GPIO8B2_5_OFFSET, 1 << GPIO8B2_5_OFFSET);
                guess = guess + 64;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[6] >> 4 == 1)
                {
                    gpio_8b2->gpio_read(&power[6], 1 << GPIO8B2_4_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[6] = !LED_State[6];
                gpio_8b2->gpio_write(LED_State[6] << GPIO8B2_5_OFFSET, 1 << GPIO8B2_5_OFFSET);
                guess = guess - 64;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[6] >> 4 == 1)
                {
                    gpio_8b2->gpio_read(&power[6], 1 << GPIO8B2_4_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b2->gpio_read(&power[5], 1 << GPIO8B2_6_OFFSET);
        if (power[5] >> 6 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[5] == 0)
            {
                LED_State[5] = !LED_State[5];
                gpio_8b2->gpio_write(LED_State[5] << GPIO8B2_7_OFFSET, 1 << GPIO8B2_7_OFFSET);
                guess = guess + 32;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[5] >> 6 == 1)
                {
                    gpio_8b2->gpio_read(&power[5], 1 << GPIO8B2_6_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[5] = !LED_State[5];
                gpio_8b2->gpio_write(LED_State[5] << GPIO8B2_7_OFFSET, 1 << GPIO8B2_7_OFFSET);
                guess = guess - 32;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[5] >> 6 == 1)
                {
                    gpio_8b2->gpio_read(&power[5], 1 << GPIO8B2_6_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b3->gpio_read(&power[4], 1 << GPIO8B3_0_OFFSET);
        if (power[4] >> 0 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[4] == 0)
            {
                LED_State[4] = !LED_State[4];
                gpio_8b3->gpio_write(LED_State[4] << GPIO8B3_1_OFFSET, 1 << GPIO8B3_1_OFFSET);
                guess = guess + 16;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[4] >> 0 == 1)
                {
                    gpio_8b3->gpio_read(&power[4], 1 << GPIO8B3_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[4] = !LED_State[4];
                gpio_8b3->gpio_write(LED_State[4] << GPIO8B3_1_OFFSET, 1 << GPIO8B3_1_OFFSET);
                guess = guess - 16;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[4] >> 0 == 1)
                {
                    gpio_8b3->gpio_read(&power[4], 1 << GPIO8B3_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b3->gpio_read(&power[3], 1 << GPIO8B3_6_OFFSET);
        if (power[3] >> 6 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[3] == 0)
            {
                LED_State[3] = !LED_State[3];
                gpio_8b3->gpio_write(LED_State[3] << GPIO8B3_7_OFFSET, 1 << GPIO8B3_7_OFFSET);
                guess = guess + 8;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[3] >> 6 == 1)
                {
                    gpio_8b3->gpio_read(&power[3], 1 << GPIO8B3_6_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[3] = !LED_State[3];
                gpio_8b3->gpio_write(LED_State[3] << GPIO8B3_7_OFFSET, 1 << GPIO8B3_7_OFFSET);
                guess = guess - 8;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[3] >> 6 == 1)
                {
                    gpio_8b3->gpio_read(&power[3], 1 << GPIO8B3_6_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b3->gpio_read(&power[2], 1 << GPIO8B3_4_OFFSET);
        if (power[2] >> 4 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[2] == 0)
            {
                LED_State[2] = !LED_State[2];
                gpio_8b3->gpio_write(LED_State[2] << GPIO8B3_5_OFFSET, 1 << GPIO8B3_5_OFFSET);
                guess = guess + 4;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[2] >> 4 == 1)
                {
                    gpio_8b3->gpio_read(&power[2], 1 << GPIO8B3_4_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[2] = !LED_State[2];
                gpio_8b3->gpio_write(LED_State[2] << GPIO8B3_5_OFFSET, 1 << GPIO8B3_5_OFFSET);
                guess = guess - 4;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[2] >> 4 == 1)
                {
                    gpio_8b3->gpio_read(&power[2], 1 << GPIO8B3_4_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_8b3->gpio_read(&power[1], 1 << GPIO8B3_2_OFFSET);
        if (power[1] >> 2 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[1] == 0)
            {
                LED_State[1] = !LED_State[1];
                gpio_8b3->gpio_write(LED_State[1] << GPIO8B3_3_OFFSET, 1 << GPIO8B3_3_OFFSET);
                guess = guess + 2;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[1] >> 2 == 1)
                {
                    gpio_8b3->gpio_read(&power[1], 1 << GPIO8B3_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[1] = !LED_State[1];
                gpio_8b3->gpio_write(LED_State[1] << GPIO8B3_3_OFFSET, 1 << GPIO8B3_3_OFFSET);
                guess = guess - 2;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[1] >> 2 == 1)
                {
                    gpio_8b3->gpio_read(&power[1], 1 << GPIO8B3_2_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        gpio_4b1->gpio_read(&power[0], 1 << GPIO4B1_0_OFFSET);
        if (power[0] >> 0 == 1)
        {
            if (checkint() == 0)
                return count_down;
            if (LED_State[0] == 0)
            {
                LED_State[0] = !LED_State[0];
                gpio_4b1->gpio_write(LED_State[0] << GPIO4B1_1_OFFSET, 1 << GPIO4B1_1_OFFSET);
                guess = guess + 1;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[0] >> 0 == 1)
                {
                    gpio_4b1->gpio_read(&power[0], 1 << GPIO4B1_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }

            else
            {
                LED_State[0] = !LED_State[0];
                gpio_4b1->gpio_write(LED_State[0] << GPIO4B1_1_OFFSET, 1 << GPIO4B1_1_OFFSET);
                guess = guess - 1;

                if (checkint() == 0)
                    return count_down;

                lcd_obj->set_CursorPos(0, 1);
                lcd_obj->printf("%-4d", guess);
                while (power[0] >> 0 == 1)
                {
                    gpio_4b1->gpio_read(&power[0], 1 << GPIO4B1_0_OFFSET);
                    if (checkint() == 0)
                        return count_down;
                }
            }
        }

        /* Add a little delay avoid clicking button immediately & noise */
        board_delay_ms(50, 0);

        if (checkint() == 0)
            return count_down;

        gpio_4b2->gpio_read(&press, 1 << GPIO4B2_0_OFFSET); //讀取Control button狀態
    }

    if (checkint() == 0)
        return count_down;

    chance = chance - 1; //若有按Control button送出本次猜測的值就將剩餘次數減一
    lcd_obj->set_CursorPos(6, 1);
    lcd_obj->printf("%2d", chance);
    if(chance == 3) //若剩餘次數等於3次讓LCD紅色閃爍
    {
        lcd_obj->set_Color(RED);
        lcd_obj->blink_LED(ON);
    }
    gpio_4b2->gpio_write(0 << GPIO4B2_1_OFFSET, 1 << GPIO4B2_1_OFFSET); //將enable write燈號轉暗
    return guess;                                                       //回傳猜測的值
}

int checkint()
{
    if ((mode_state[1] == 1) && (ifint == true)) //若有進interrupt則秒數減一
    {
        count_down = count_down - 1;

        lcd_obj->set_CursorPos(13, 1);
        lcd_obj->printf("%3d", count_down);
        lcd_obj->home();
    }

    if ((mode_state[1] == 1) && (count_down == 50)) //若秒數數到50讓LCD紅色閃爍
    {
        lcd_obj->set_Color(RED);
        lcd_obj->blink_LED(ON);
    }

    if ((mode_state[1] == 1) && (count_down <= 0)) //若秒數數到0的時候回傳0
    {
        ifint = false; //將flag設回false
        return 0;
    }
    else //其餘狀況回傳1
    {
        ifint = false; //將flag設回false
        return 1;
    }
}

void t1_isr(void *ptr) //在ISR中將flag設為true代表有進中斷
{
    // Clear IP first
    timer_int_clear(TIMER_1);

    ifint = true;
}

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

// =====================【配置宏】=====================
#define MAX_WEEKS 20
#define MAX_COURSES 12
int total_weeks = 18;
int total_courses = 0;

#define FONT_FILE "NotoSansSCLight-3.ttf"
#define FONT_SIZE 14

#define CONFIG_TXT "config.txt"
#define SAVE_TXT "save.txt"
// ======================================================================
#define CELL_W 70
#define CELL_H 45
#define MARGIN_TOP 80
#define MARGIN_LEFT 120

bool grid[MAX_COURSES][MAX_WEEKS] = {false};
// 格式："2026.9.1"
char week_start_date[MAX_WEEKS][24];
char course_names[MAX_COURSES][64];

SDL_Window* win = NULL;
SDL_Renderer* ren = NULL;
TTF_Font* font = NULL;

// 去除行末尾 \r \n
static void trim_newline(char *buf)
{
    size_t len = strlen(buf);
    while(len>0 && (buf[len-1]=='\n' || buf[len-1]=='\r')){
        buf[len-1]=0;
        len--;
    }
}

/**
 * 获取月份天数，支持平年（本程序不处理闰年，2月固定28天）
 * mon:1‑12
 */
static int get_month_days(int mon)
{
    switch(mon)
    {
        case 1: return 31;
        case 2: return 28;
        case 3: return 31;
        case 4: return 30;
        case 5: return 31;
        case 6: return 30;
        case 7: return 31;
        case 8: return 31;
        case 9: return 30;
        case 10: return 31;
        case 11: return 30;
        case 12: return 31;
        default: return 31;
    }
}

/**
 * 日期增加N天，支持全年，支持跨年
 * year,mon,day:输入输出
 * add:增加天数
 */
static void add_days(int *year, int *mon, int *day, int add)
{
    int y = *year;
    int m = *mon;
    int d = *day;
    for(int i=0;i<add;i++)
    {
        d++;
        int md = get_month_days(m);
        if(d > md)
        {
            d = 1;
            m++;
            if(m>12)
            {
                m = 1;
                y++;
            }
        }
    }
    *year = y;
    *mon = m;
    *day = d;
}

/**
 * 读取config.txt
 * 支持:
 * start_year=2026（可选，默认2026）
 * first_start=9.1
 * 课程一行一门
 */
int load_config_txt(void)
{
    FILE *fp = fopen(CONFIG_TXT,"r");
    if(!fp){
        printf("错误：无法打开 %s，请放在exe同目录\n",CONFIG_TXT);
        return -1;
    }
    char buf[256];
    total_courses = 0;
    char first_start[16]={0};
    int start_year = 2026;

    while(fgets(buf,sizeof(buf),fp))
    {
        trim_newline(buf);
        if(buf[0]=='#' || buf[0]==0) continue;

        //解析 start_year=xxxx
        if(strstr(buf,"start_year=")==buf){
            sscanf(buf+strlen("start_year="),"%d",&start_year);
            continue;
        }
        //解析 first_start=xx.x
        if(strstr(buf,"first_start=")==buf){
            strncpy(first_start, buf+strlen("first_start="), sizeof(first_start)-1);
            continue;
        }
        //课程行
        if(total_courses < MAX_COURSES){
            strncpy(course_names[total_courses], buf, sizeof(course_names[total_courses])-1);
            total_courses++;
        }
    }
    fclose(fp);

    if(total_courses <=0){
        printf("config.txt 没有读到有效课程\n");
        return -2;
    }
    if(first_start[0]==0){
        printf("config.txt 缺少 first_start= 配置\n");
        return -3;
    }

    //解析 "9.1" → 月，日
    int start_mon, start_day;
    if(sscanf(first_start,"%d.%d",&start_mon,&start_day)!=2)
    {
        printf("first_start格式错误，应为 月.日 例如 9.1\n");
        return -4;
    }

    //生成每一周的起始日期，每次+7天，支持跨年
    int cur_y = start_year;
    int cur_m = start_mon;
    int cur_d = start_day;
    for(int w=0;w<total_weeks;w++)
    {
        snprintf(week_start_date[w], sizeof(week_start_date[w]),"%d.%d.%d",cur_y,cur_m,cur_d);
        add_days(&cur_y, &cur_m, &cur_d,7);
    }

    printf("配置加载成功：课程数=%d,第一周=%s\n",total_courses,week_start_date[0]);
    return 0;
}

//读取存档 save.txt
int load_save_txt(void)
{
    FILE* fp = fopen(SAVE_TXT,"r");
    if(!fp){
        printf("save.txt不存在，使用空白课表\n");
        return 0;
    }
    for(int c=0;c<MAX_COURSES;c++){
        for(int w=0;w<MAX_WEEKS;w++){
            int v=0;
            if(fscanf(fp,"%d",&v)==1){
                grid[c][w] = (v!=0);
            }else{
                grid[c][w]=false;
            }
        }
    }
    fclose(fp);
    printf("已加载上次保存课表\n");
    return 0;
}

//Ctrl+S 保存
int save_grid_to_txt(void)
{
    FILE* fp = fopen(SAVE_TXT,"w");
    if(!fp){
        printf("保存失败，无法打开 %s\n",SAVE_TXT);
        return -1;
    }
    for(int c=0;c<MAX_COURSES;c++){
        for(int w=0;w<MAX_WEEKS;w++){
            fprintf(fp,"%d ", grid[c][w]?1:0);
        }
        fprintf(fp,"\n");
    }
    fclose(fp);
    printf("✅ Ctrl+S 已保存到 %s\n",SAVE_TXT);
    return 0;
}

// 渲染UTF8中文文字到窗口
void render_text(const char* txt, int x, int y, SDL_Color color)
{
    if (!font || !txt) return;
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, txt, color);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    if (tex != NULL)
    {
        SDL_Rect dst = {x, y, surf->w, surf->h};
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_FreeSurface(surf);
}

// 绘制整个课表界面
void render_scene()
{
    SDL_SetRenderDrawColor(ren, 255, 255, 255, 255);
    SDL_RenderClear(ren);
    SDL_Color black = {0,0,0,255};
    // 顶部横轴：周号+起始日期（格式：2026.9.1）
    for(int w = 0; w < total_weeks; w++)
    {
        int x = MARGIN_LEFT + w * CELL_W;
        SDL_Rect head_rect = {x, 10, CELL_W, MARGIN_TOP - 10};
        SDL_SetRenderDrawColor(ren, 220,240,255,255);
        SDL_RenderFillRect(ren, &head_rect);
        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderDrawRect(ren, &head_rect);
        char buf[64];
        snprintf(buf, sizeof(buf), "第%d周", w+1);
        render_text(buf, x+4, 12, black);
        render_text(week_start_date[w], x+2, 32, black);
    }
    // 左侧纵轴：课程名称
    for(int c = 0; c < total_courses; c++)
    {
        int y = MARGIN_TOP + c * CELL_H;
        SDL_Rect course_rect = {0, y, MARGIN_LEFT, CELL_H};
        SDL_SetRenderDrawColor(ren, 240,240,240,255);
        SDL_RenderFillRect(ren, &course_rect);
        SDL_SetRenderDrawColor(ren,0,0,0,255);
        SDL_RenderDrawRect(ren, &course_rect);
        render_text(course_names[c], 8, y+12, black);
    }
    // 网格单元格
    for(int c = 0; c < total_courses; c++)
    {
        for(int w = 0; w < total_weeks; w++)
        {
            int x = MARGIN_LEFT + w * CELL_W;
            int y = MARGIN_TOP + c * CELL_H;
            SDL_Rect cell = {x, y, CELL_W, CELL_H};
            if(grid[c][w]){
                SDL_SetRenderDrawColor(ren, 80,180,100,255); //绿色选中
            }else{
                SDL_SetRenderDrawColor(ren, 250,250,250,255); //灰色未选
            }
            SDL_RenderFillRect(ren, &cell);
            SDL_SetRenderDrawColor(ren, 100,100,100,255);
            SDL_RenderDrawRect(ren, &cell);
        }
    }
    SDL_RenderPresent(ren);
}

// 鼠标点击翻转格子状态
void mouse_click(int mx, int my)
{
    if(mx < MARGIN_LEFT || my < MARGIN_TOP) return;
    int w = (mx - MARGIN_LEFT) / CELL_W;
    int c = (my - MARGIN_TOP) / CELL_H;
    if(w >= 0 && w < total_weeks && c >=0 && c < total_courses)
    {
        grid[c][w] = !grid[c][w];
    }
}

int main(int argc, char* argv[])
{
    if(load_config_txt() != 0){
        return -1;
    }
    load_save_txt();

    if(SDL_Init(SDL_INIT_VIDEO) <0)
    {
        printf("SDL初始化失败：%s\n", SDL_GetError());
        return -1;
    }
    if(TTF_Init() == -1)
    {
        printf("TTF初始化失败：%s\n", TTF_GetError());
        SDL_Quit();
        return -1;
    }
    // 加载字体
    font = TTF_OpenFont(FONT_FILE, FONT_SIZE);
    if(!font)
    {
        printf("字体加载失败 %s : %s\n", FONT_FILE, TTF_GetError());
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    int win_w = MARGIN_LEFT + total_weeks * CELL_W + 20;
    int win_h = MARGIN_TOP + total_courses * CELL_H + 20;
    win = SDL_CreateWindow("课程周次表 Ctrl+S保存",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, win_w, win_h, SDL_WINDOW_SHOWN);
    if (!win)
    {
        printf("窗口创建失败: %s\n", SDL_GetError());
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren)
    {
        printf("渲染器创建失败: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        TTF_CloseFont(font);
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    bool quit = false;
    SDL_Event e;
    while(!quit)
    {
        while(SDL_PollEvent(&e) != 0)
        {
            if(e.type == SDL_QUIT) quit = true;
            if(e.type == SDL_MOUSEBUTTONDOWN)
            {
                if(e.button.button == SDL_BUTTON_LEFT)
                {
                    mouse_click(e.button.x, e.button.y);
                }
            }
            // Ctrl+S保存快捷键
            if(e.type == SDL_KEYDOWN)
            {
                SDL_Keymod mod = SDL_GetModState();
                if( (mod & KMOD_CTRL) && e.key.keysym.sym == SDLK_s )
                {
                    save_grid_to_txt();
                }
            }
        }
        render_scene();
    }
    // 释放资源
    TTF_CloseFont(font);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

/* SPDX-License-Identifier: MIT. Original 5x7 stroke alphabet, no font dependency.
 * Vertical strokes use integer scaling (3/4/5 pixels at SD/720/1080).
 * Horizontal edges are rounded in DISPLAY coordinates, compensating non-square
 * pixels, including both SD anamorphic aspects. Never shrink text to fit. */
#include "ui_layout.h"
#include <stdio.h>
#include <string.h>

static const unsigned char capitals[26][7]={
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
 {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
 {7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
 {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
 {17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static const unsigned char lower[26][7]={
 {0,0,14,1,15,17,15},{16,16,30,17,17,17,30},{0,0,14,16,16,17,14},
 {1,1,15,17,17,17,15},{0,0,14,17,31,16,14},{6,9,8,28,8,8,8},
 {0,0,15,17,17,15,1},{16,16,30,17,17,17,17},{4,0,12,4,4,4,14},
 {2,0,6,2,2,18,12},{16,16,18,20,24,20,18},{12,4,4,4,4,4,14},
 {0,0,26,21,21,21,21},{0,0,30,17,17,17,17},{0,0,14,17,17,17,14},
 {0,0,30,17,17,30,16},{0,15,17,17,15,1,1},{0,0,22,25,16,16,16},
 {0,0,15,16,14,1,30},{8,8,28,8,8,9,6},{0,0,17,17,17,19,13},
 {0,0,17,17,17,10,4},{0,0,17,17,21,21,10},{0,0,17,10,4,10,17},
 {0,17,17,17,15,1,14},{0,0,31,2,4,8,31}};
static const unsigned char digits[10][7]={
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},
 {30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},
 {14,17,17,15,1,1,14}};
static unsigned bits(unsigned char c,int row)
{
 if(row==7)return c=='g'?14:c=='p'?16:0; /* descenders below baseline */
 if(c>='A'&&c<='Z')return capitals[c-'A'][row];
 if(c>='a'&&c<='z')return lower[c-'a'][row];
 if(c>='0'&&c<='9')return digits[c-'0'][row];
 switch(c){
 case ' ':return 0; case '.':return row==6?4:0; case ',':return row==5?4:row==6?8:0;
 case ':':return row==2||row==5?4:0;case ';':return row==2||row==5?4:row==6?8:0;
 case '!':return row<4||row==6?4:0;case '?':{static const unsigned char a[]={14,17,1,2,4,0,4};return a[row];}
 case '-':return row==3?14:0;case '_':return row==6?31:0;case '=':return row==2||row==4?31:0;
 case '+':return row==3?31:row>0&&row<6?4:0;case '/':return 1u<<(row<2?0:row<3?1:row<5?2:row<6?3:4);
 case '\\':return 1u<<(row<2?4:row<3?3:row<5?2:row<6?1:0);
 case '(':return row==0||row==6?2:row==1||row==5?4:8;
 case ')':return row==0||row==6?8:row==1||row==5?4:2;
 case '[':return row==0||row==6?14:8;case ']':return row==0||row==6?14:2;
 case '<':return row==3?16:row==2||row==4?8:row==1||row==5?4:0;
 case '>':return row==3?1:row==2||row==4?2:row==1||row==5?4:0;
 case '\'':return row<2?4:0;case '"':return row<2?10:0;
 case '%':return row==0||row==1?25:row==5||row==6?19:1u<<(row-1);
 case '#':return row==2||row==4?31:10;case '*':return row==2||row==4?10:row==3?4:0;
 case '|':return 4;case '&':{static const unsigned char a[]={12,18,20,8,21,18,13};return a[row];}
 default:return bits('?',row);
 }
}
/* A screen pixel has display width aspect*height/width, in line-height units. */
static int edge(const ui_layout *l,int units,int scale)
{
 int numerator=units*scale*l->width*(l->wide?9:3);
 int denominator=l->height*(l->wide?16:4);
 return (numerator+denominator/2)/denominator;
}
void ui_layout_init(ui_layout *l,int w,int h,int wide,int interlaced)
{
 memset(l,0,sizeof(*l));l->width=w;l->height=h;l->wide=!!wide;l->interlaced=!!interlaced;
 int margin=h<=576?10:5;
 l->left=(w*margin+99)/100;l->right=w-l->left;
 l->top=(h*margin+99)/100;l->bottom=h-l->top;
 l->scale=(h*3+699)/700;l->title_scale=l->scale+1;
 if(l->interlaced&&l->scale<2)l->scale=2;
 if(l->interlaced&&l->title_scale<2)l->title_scale=2;
 l->advance=edge(l,6,l->scale);l->line=8*l->scale;l->gap=l->scale*2;
 l->columns=(l->right-l->left)/l->advance;
 /* Title, at most two subtitle lines, and two hint lines have reserved space. */
 l->body_top=l->top+7*l->title_scale+l->gap+2*l->line+l->gap;
 l->body_bottom=l->bottom-2*l->line-l->gap;
 l->lines=(l->body_bottom-l->body_top)/l->line;
}
static void rect(ui_canvas *c,int x,int y,int w,int h,uint32_t color)
{
 if(!c->pixels)return;
 for(int j=0;j<h;j++)for(int i=0;i<w;i++)c->pixels[(y+j)*c->stride+x+i]=color;
}
static void line(const ui_layout *l,ui_canvas *c,ui_result *r,int x,int y,int scale,int item,const char *s,uint32_t color)
{
 if(l->interlaced&&color==0xfff4f6f8u)color=0xffdce0e4u; /* about 90% white */
 int advance=edge(l,6,scale),n=(int)strlen(s),w=n?advance*(n-1)+edge(l,5,scale):0;
 int rows=(strchr(s,'g')||strchr(s,'p'))?8:7;
 if(c->observe)c->observe(c->user,x,y,w,rows*scale,item,s);
 r->text_lines++;
 for(int k=0;k<n;k++)for(int row=0;row<8;row++){
  unsigned b=bits((unsigned char)s[k],row);
  for(int col=0;col<5;col++)if(b&(16u>>col)){
   int a=edge(l,col,scale),z=edge(l,col+1,scale);
   rect(c,x+k*advance+a,y+row*scale,z-a,scale,color);
  }
 }
}
/* Advance one wrapped line, splitting paths/hashes too. One replacement per
 * unsupported UTF-8 codepoint; no continuation byte expands the line width. */
static const char *wrap(const char *s,int columns,char *out)
{
 int n=0,word=-1;const char *at=s,*word_next=NULL;
 while(*at&&*at!='\n'&&n<columns){
  unsigned char ch=(unsigned char)*at++;
  if(ch>=128){while(((unsigned char)*at&0xc0)==0x80)at++;ch='?';}
  out[n++]=(char)(ch=='\t'?' ':ch);
  if(ch==' '){word=n-1;word_next=at;}
 }
 if(*at&&*at!='\n'&&n==columns&&word>0){n=word;at=word_next;}
 while(n&&out[n-1]==' ')n--;out[n]=0;
 if(*at=='\n')at++;else while(*at==' ')at++;
 return at;
}
static int wrapped_count(const char *s,int cols)
{int n=0;char b[256];do{s=wrap(s,cols,b);n++;}while(*s);return n;}
static void block(const ui_layout *l,ui_canvas *c,ui_result *r,const char *text,int y,int rows,int cols,int item,uint32_t color)
{
 char b[256];const char *s=text;
 for(int j=0;j<rows;j++){
  s=wrap(s,cols,b);
  if(j==rows-1&&*s){int n=(int)strlen(b);while(n<cols)b[n++]=' ';b[n]=0;memcpy(b+cols-3,"...",3);}
  line(l,c,r,l->left,y+j*l->line,l->scale,item,b,color);
  if(!*s)break;
 }
}
int ui_render(const ui_layout *base,const ui_view *v,ui_canvas *c,ui_result *r)
{
 if(!base||!v||!c||!r||base->columns<12||base->columns>250||base->lines<1)return -1;
 ui_layout placed=*base;const ui_layout *l=&placed;
 const char *title=v->title?v->title:(v->question?"Please confirm":"Status");
 int header_rows=wrapped_count(title,l->columns);if(header_rows>3)header_rows=3;
 placed.body_top=l->top+7*l->title_scale+l->gap+header_rows*l->line+l->gap;
 placed.lines=(l->body_bottom-l->body_top)/l->line;
 memset(r,0,sizeof(*r));r->first=r->last=-1;
 r->body_top=l->body_top;r->body_lines=l->lines;
 rect(c,0,0,l->width,l->height,UI_BACKGROUND);
 line(l,c,r,l->left,l->top,l->title_scale,-1,"Buzz! Bridge",0xfff4f6f8);
 block(l,c,r,title,l->top+7*l->title_scale+l->gap,header_rows,l->columns,-1,0xffb6d9ed);
 char hints[256];int hint_y=l->bottom-2*l->line;
 if(v->items&&v->count>0){
  int selected=v->selected>=0&&v->selected<v->count?v->selected:0;
  /* Variable-height rows. Keep the selection and as many preceding rows as fit. */
  int first=selected,budget=l->body_bottom-l->body_top,used=0;
  for(int i=selected;i>=0;i--){
   int rows=wrapped_count(v->items[i],l->columns-2);if(rows>3)rows=3;
   int cost=rows*l->line+l->gap;
   if(used+cost>budget&&i!=selected)break;
   first=i;used+=cost;
  }
  int y=l->body_top;
  for(int i=first;i<v->count;i++){
   int rows=wrapped_count(v->items[i],l->columns-2);if(rows>3)rows=3;
   if(rows*l->line>l->body_bottom-y)break;
   char b[256];const char *s=v->items[i];
   for(int j=0;j<rows;j++){
    s=wrap(s,l->columns-2,b+2);b[0]=i==selected&&j==0?'>':' ';b[1]=' ';
    if(j==rows-1&&*s){int n=(int)strlen(b);while(n<l->columns)b[n++]=' ';b[n]=0;memcpy(b+l->columns-3,"...",3);}
    line(l,c,r,l->left,y,l->scale,i,b,i==selected?0xffffe080:0xfff4f6f8);y+=l->line;
   }
   if(r->first<0)r->first=i;r->last=i;if(i==selected)r->selection_visible=1;
   y+=l->gap;
  }
  snprintf(hints,sizeof(hints),"D-pad: move  Cross: select\nCircle: back  %d/%d",selected+1,v->count);
  r->pages=1;
 }else{
  const char *s=v->text?v->text:"Please wait...";
  int total=wrapped_count(s,l->columns);r->pages=(total+l->lines-1)/l->lines;
  r->page=v->page<0?0:v->page>=r->pages?r->pages-1:v->page;
  char b[256];for(int i=0;i<r->page*l->lines;i++)s=wrap(s,l->columns,b);
  for(int j=0;j<l->lines;j++){
   s=wrap(s,l->columns,b);
   line(l,c,r,l->left,l->body_top+j*l->line,l->scale,-1,b,0xfff4f6f8);
   if(!*s)break;
  }
  if(v->question&&r->page==r->pages-1)
   snprintf(hints,sizeof(hints),"Left/Right: %s\nCross: choose Circle: back",v->yes?"[YES] / No":"Yes / [NO]");
  else if(v->question||v->page>=0)
   snprintf(hints,sizeof(hints),"Up/Down: page %d/%d\nCross: %s  Circle: back",r->page+1,r->pages,r->page+1<r->pages?"next":"OK");
  else snprintf(hints,sizeof(hints),"Please wait...\nPS: system menu / quit");
 }
 block(l,c,r,hints,hint_y,2,l->columns,-1,0xffb6d9ed);
 if(l->interlaced&&c->pixels)ui_filter_interlaced(c,l->width,l->height);
 return 0;
}

/* Symmetric [1 2 1]/4 in numeric XRGB, independent of PPU/host byte order.
 * Read original rows with one saved scanline; replicate top/bottom edges.
 * Only software memory is read/modified, never the RSX display buffers.
 * Every glyph (including punctuation/selection marker) uses >=2 vertical
 * pixels before filtering; future rules must use the same minimum thickness.
 */
void ui_filter_interlaced(ui_canvas *c,int width,int height)
{
 if(!c->pixels||width<1||width>1920||height<1)return;
 uint32_t previous[1920];memcpy(previous,c->pixels,(size_t)width*4);
 for(int y=0;y<height;y++)for(int x=0;x<width;x++){
  uint32_t *p=c->pixels+y*c->stride+x,a=previous[x],b=*p;
  uint32_t next=y+1<height?p[c->stride]:b,color=0xff000000u;
  for(int shift=0;shift<24;shift+=8){
   unsigned v=((a>>shift)&255u)+2*((b>>shift)&255u)+((next>>shift)&255u);
   color|=(v/4)<<shift;
  }
  previous[x]=b;*p=color;
 }
}

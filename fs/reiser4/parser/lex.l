%Start Normal

LETTER          [A-Za-z_]
DIG             [0-9]
WRD             ({LETTER}|{DIG})+
SP              [ \t\n]+
SPECIAL         [\[\]\{\}\/\\\,\:\;\*\$\@\!\`\']



%%

%{
BEGIN Normal;
%}

{SP}"/"{SP}                                       {return BLANK_SLASH_BLANK;}

{SP}?";"{SP}?                                     {return SEMICOLON;}
{SP}?","{SP}?                                     {return COMMA;}
{SP}?"+"{SP}?                                     {return PLUS;}
{SP}?"("{SP}?                                     {return L_PARENT;}
{SP}?")"{SP}?                                     {return R_PARENT;}
{SP}?"{"{SP}?                                     {return L_FLX_PARENT;}
{SP}?"}"{SP}?                                     {return R_FLX_PARENT;}
{SP}?"["{SP}?                                     {return L_SKW_PARENT;}
{SP}?"]"{SP}?                                     {return R_SKW_PARENT;}

{SP}?eq{SP}?                                      {return EQ;}
{SP}?ne{SP}?                                      {return NE;}
{SP}?le{SP}?                                      {return LE;}
{SP}?ge{SP}?                                      {return GE;}
{SP}?lt{SP}?                                      {return LT;}
{SP}?gt{SP}?                                      {return GT;}
{SP}?is{SP}?                                      {return IS;}
{SP}?and{SP}?                                     {return AND;}
{SP}?or{SP}?                                      {return OR;}
{SP}?not{SP}?                                     {return NOT;}
{SP}?if{SP}?                                      {return IF;}
{SP}?then{SP}?                                    {return THEN;}
{SP}?else{SP}?                                    {return ELSE;}
{SP}?exist{SP}?                                   {return EXIST;}

{SP}?"<""-"{SP}?                                  {return L_ASSIGN;}
{SP}?"<""-""="{SP}?                               {return L_SYMLINK;}

{SP}?tw"/""("{SP}?                                {return TRANSCRASH;}

"/"process                                        {return SLASH_PROCESS;}
"/"stat                                           {return SLASH_STAT;}
"/"range                                          {return SLASH_RANGE;}
"/""("                                            {return SLASH_L_PARENT;}
"/"                                               {return SLASH;}


{SP}?"]"{SP}?                                     {return BLANK_SLASH_BLANK;}


{WRD}                                             { return  WORD ;}

.                                                 { return  0 ;}
%%




#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t b(uint64_t opc, uint32_t start, uint32_t count) {
  return (opc >> start) & ((1 << count) - 1);
}

void DisasmYmDSP(const int isAICA, uint16_t MPRO[], int LastStep,
                 int16_t COEFS[], uint16_t MADRS[]) {
  char input[32];

  int i, step;
  uint64_t opc;
  uint32_t last_coef = 0, last_madrs = 0;
  uint32_t TRA, TWT, TWA, XSEL, YSEL, IRA, IWT, IWA, TABLE, MWT, MRD, EWT, EWA,
      ADRL, FRCL, SHIFT, YRL, NEGB, ZERO, BSEL, NOFL, COEF, MASA, ADREB, NXADR;

  if (LastStep == 0)
    return;

  for (i = 0; i < 128; i++) {
    int16_t v = COEFS[i << !!isAICA] / 8;
    printf("COEF%02X:%04X: %+6d %4.0f%% %8.5f\n", i, v & 0xFff, v,
           (float)v / 40.95f, (float)v / 4096.f);
  }
  printf("\n");
  for (i = 0; i < ((isAICA) ? 64 : 32); i++) {
    uint16_t v = MADRS[i << !!isAICA];
    printf("MADRS%02X:%04X: %6u %5.1fms\n", i, v & 0xFFff, v, (float)v / 44.1f);
  }
  printf("\n");

  for (step = 0; step < LastStep; step++) {
    if (isAICA) {
      opc = (uint64_t)MPRO[0 + 8 * step] << 48 |
            (uint64_t)MPRO[2 + 8 * step] << 32 |
            (uint64_t)MPRO[4 + 8 * step] << 16 | (uint64_t)MPRO[6 + 8 * step];
    } else {
      opc = ((uint64_t)MPRO[0 + 4 * step]) << 48 |
            ((uint64_t)MPRO[1 + 4 * step]) << 32 |
            ((uint64_t)MPRO[2 + 4 * step]) << 16 |
            ((uint64_t)MPRO[3 + 4 * step]);
    }

    printf("%02X:", step);
    printf("%04llX %04llX %04llX %04llX   ", (opc >> 48) & 0xFFff,
           (opc >> 32) & 0xFFff, (opc >> 16) & 0xFFff, (opc)&0xFFff);

    if (isAICA) {
      TRA = b(opc,  9 + 48, 7);
      TWT = b(opc,  8 + 48, 1);
      TWA = b(opc,  1 + 48, 7);
    } else {
      TRA = b(opc,  8 + 48, 7);
      TWT = b(opc,  7 + 48, 1);
      TWA = b(opc,  0 + 48, 7);
    }

    XSEL  = b(opc, 15 + 32, 1);
    YSEL  = b(opc, 13 + 32, 2);
    if (isAICA) {
      IRA = b(opc,  7 + 32, 6);
      IWT = b(opc,  6 + 32, 1);
      IWA = b(opc,  1 + 32, 5);
    } else {
      IRA = b(opc,  6 + 32, 6);
      IWT = b(opc,  5 + 32, 1);
      IWA = b(opc,  0 + 32, 5);
    }

    TABLE = b(opc, 15 + 16, 1);
    MWT   = b(opc, 14 + 16, 1);
    MRD   = b(opc, 13 + 16, 1);
    EWT   = b(opc, 12 + 16, 1);
    EWA   = b(opc,  8 + 16, 4);
    ADRL  = b(opc,  7 + 16, 1);
    FRCL  = b(opc,  6 + 16, 1);
    SHIFT = b(opc,  4 + 16, 2);
    YRL   = b(opc,  3 + 16, 1);
    NEGB  = b(opc,  2 + 16, 1);
    ZERO  = b(opc,  1 + 16, 1);
    BSEL  = b(opc,  0 + 16, 1);

    NOFL    = b(opc, 15 + 0, 1);
    if (isAICA) {
      COEF  = step;
      MASA  = b(opc,  9 + 0, 6);
      ADREB = b(opc,  8 + 0, 1);
      NXADR = b(opc,  7 + 0, 1);
    } else {
      COEF  = b(opc,  9 + 0, 6);
      MASA  = b(opc,  2 + 0, 5);
      ADREB = b(opc,  1 + 0, 1);
      NXADR = b(opc,  0 + 0, 1);
    }

    if (IRA <= 0x1f)
      sprintf(input, "MEMS%02X", IRA);
    else if (IRA <= 0x2f)
      sprintf(input, "MIXS%02X", IRA - 0x20);
    else
      sprintf(input, "EXTS%02X", IRA - 0x30);

    if (XSEL == 0 && YSEL == 0 && ZERO == 0 && BSEL == 0 && NEGB == 0) {
      printf("NOP");
    } else {
      printf((ZERO) ? "MPY" : "AMPY REG");

      if (XSEL) {
        printf(" %s", input);
      } else {
        printf(" TEMP%02X", TRA);
      }

      switch (YSEL) {
      case 0:
        printf(" FREG");
        break;
      case 1:
        printf(" COEF%02X", COEF);
        if (last_coef < COEF)
          last_coef = COEF;
        break;
      case 2:
        printf(" (YREG>>11)");
        break;
      case 3:
        printf(" (YREG>>4)");
        break;
      }

      if (ZERO) {
        /* printf("+0 "); */
      } else {
        printf((NEGB) ? " -" : " ");
        if (BSEL) { /*printf("ACC");*/
        } else {
          printf("TEMP%02X", TRA);
        }
      }
    }

    if (IWT) {
      printf(" LDI MEMS%02X DREG", IWA);
    }

    if (TWT) {
      printf(" STR");
      if (SHIFT > 0)
        printf(" S%u", SHIFT);
      printf(" TEMP%02X", TWA);
    }
    if (EWT) {
      printf(" STR");
      if (SHIFT > 0)
        printf(" S%u", SHIFT);
      printf(" EFREG%01X", EWA);
    }

    if (TWT == 0 && EWT == 0)
      if (SHIFT > 0)
        printf(" STR S%u DREG", SHIFT);

    if (YRL) {
      printf(" LDY %s", input);
    }

    if (MWT || MRD) {
      if (MWT)
        printf(" MW ");
      if (MRD)
        printf(" MR ");
      printf("MADRS%02X", MASA);
      if (last_madrs < MASA)
        last_madrs = MASA;
      if (!TABLE)
        printf("+DEC");
      if (NXADR)
        printf("+1");
      if (ADREB)
        printf("/ADREB");
      if (NOFL)
        printf("/NOFL");
    }

    if (ADRL) {
      if (SHIFT == 3) {
        printf(" LDA (shited)");
      } else {
        printf(" LDA %s", input);
      }
    }

    if (FRCL) {
      if (SHIFT == 3) {
        printf(" FRCL<-(shifted)");
      } else {
        printf(" FRCLA<-%s", input);
      }
    }

    printf("\n");
  }
  printf("Last used COEF%02X and MADRS%02X\n", last_coef, last_madrs);
}

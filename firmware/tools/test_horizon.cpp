// =====================================================================
//  test_horizon.cpp — verificación numérica del Horizon Lock en el PC.
//  Usa EXACTAMENTE el mismo fc_horizon.h que corre en el ESP32.
//
//  Compilar:  g++ -std=c++17 -O2 -I../esp32s3_flexcam test_horizon.cpp -o test_horizon
//  Ejecutar:  ./test_horizon
// =====================================================================
#include "fc_horizon.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <string>

static int fallos = 0, pruebas = 0;

static void check(const char* nombre, bool ok, const std::string& extra = "") {
  pruebas++;
  if (!ok) fallos++;
  printf("%s  %s%s%s\n", ok ? "PASA " : "FALLA", nombre,
         extra.empty() ? "" : "  ->  ", extra.c_str());
}
static std::string f(const char* fmt, double a, double b = 0, double c = 0) {
  char buf[160]; snprintf(buf, sizeof(buf), fmt, a, b, c); return buf;
}

// --- cuaterniones -----------------------------------------------------
struct Q { float x, y, z, w; };
static Q mul(const Q& a, const Q& b) {
  return { a.w*b.x + a.x*b.w + a.y*b.z - a.z*b.y,
           a.w*b.y - a.x*b.z + a.y*b.w + a.z*b.x,
           a.w*b.z + a.x*b.y - a.y*b.x + a.z*b.w,
           a.w*b.w - a.x*b.x - a.y*b.y - a.z*b.z };
}
static Q axisAngle(float ax, float ay, float az, float deg) {
  float r = deg * (float)M_PI / 180.0f * 0.5f;
  float s = sinf(r);
  return { ax*s, ay*s, az*s, cosf(r) };
}

// Pose base: cámara mirando al horizonte y nivelada.
// Ejes del cuerpo -> X = derecha (este), Y = abajo, Z = eje óptico (norte).
// Esa base es Rx(-90) en cuaternión.
static const Q Q_BASE = { -0.70710678f, 0.0f, 0.0f, 0.70710678f };

// Cámara nivelada, girada 'roll' grados sobre su eje óptico y con la
// óptica levantada 'pitch' grados.
static Q pose(float rollDeg, float pitchDeg) {
  // pitch: giro sobre el eje X del cuerpo (derecha) ANTES de la base
  Q qPitch = axisAngle(1, 0, 0, pitchDeg);
  Q qRoll  = axisAngle(0, 0, 1, rollDeg);      // sobre el eje óptico
  return mul(mul(Q_BASE, qPitch), qRoll);
}

static void horizonOf(const Q& q, uint8_t plane, float* ang, float* conf) {
  float gx, gy, gz;
  fchGravityFromQuat(q.x, q.y, q.z, q.w, &gx, &gy, &gz);
  fchHorizonFromGravity(gx, gy, gz, plane, ang, conf);
}

int main() {
  printf("=== 1. Referencia: cámara nivelada mirando al horizonte ===\n");
  {
    float a, c; horizonOf(pose(0, 0), FCH_PLANE_XY, &a, &c);
    check("nivelada da horizonte 0", fabsf(a) < 0.01f, f("%.4f grados", a));
    check("confianza maxima con la optica horizontal", c > 0.999f, f("conf=%.4f", c));
    float gx, gy, gz;
    Q q = pose(0, 0);
    fchGravityFromQuat(q.x, q.y, q.z, q.w, &gx, &gy, &gz);
    check("gravedad cae por 'abajo' de la camara (0,1,0)",
          fabsf(gx) < 1e-3f && fabsf(gy - 1.0f) < 1e-3f && fabsf(gz) < 1e-3f,
          f("g=(%.3f, %.3f, %.3f)", gx, gy, gz));
  }

  printf("\n=== 2. Angulos exigidos: 0,45,90,179,180,181,270,360,720 ===\n");
  printf("       (el horizonte es la inclinacion del mundo DENTRO de la imagen,\n");
  printf("        por eso sale con signo contrario al giro fisico)\n");
  const float ang[] = {0, 45, 90, 179, 180, 181, 270, 360, 720};
  for (float a : ang) {
    float h, c; horizonOf(pose(a, 0), FCH_PLANE_XY, &h, &c);
    float esperado = fchWrap180(-a);
    bool ok = fabsf(fchAngDiff(h, esperado)) < 0.02f;
    check((std::string("giro fisico ") + std::to_string((int)a) + " grados").c_str(),
          ok, f("horizonte=%.3f  esperado=%.3f", h, esperado));
  }

  printf("\n=== 2b. PRUEBA DE FONDO: el contenido queda nivelado ===\n");
  printf("       vertical_en_pantalla = horizonte + compensacion + rotacion_manual\n");
  for (float giroFisico : {0.0f, 12.0f, 45.0f, 90.0f, 179.0f, 181.0f, 270.0f, 359.0f, 721.0f}) {
    // Referencia tomada con la camara nivelada
    float hRef, cRef; horizonOf(pose(0, 0), FCH_PLANE_XY, &hRef, &cRef);
    float h, c; horizonOf(pose(giroFisico, 0), FCH_PLANE_XY, &h, &c);
    // Continuo, como lo hace el firmware
    FcHorizonState st; fchReset(&st);
    uint32_t t = 0;
    for (float a = 0.0f; a <= fabsf(giroFisico); a += 1.0f) {
      float hh, cc; horizonOf(pose(giroFisico >= 0 ? a : -a, 0), FCH_PLANE_XY, &hh, &cc);
      fchUpdate(&st, hh, cc > 0.17f, t); t += 10;
    }
    for (int i = 0; i < 300; i++) {   // dejar asentar el filtro
      fchUpdate(&st, h, c > 0.17f, t); t += 10;
    }
    float comp = -(st.filt - hRef);
    // Inclinacion del mundo en pantalla tras aplicar la compensacion:
    float enPantalla = fchWrap180(fchWrap180(-giroFisico) + comp);
    check((std::string("con giro fisico ") + std::to_string((int)giroFisico) +
           " el mundo queda vertical").c_str(),
          fabsf(enPantalla) < 0.6f,
          f("compensacion=%.2f  inclinacion residual en pantalla=%.3f", comp, enPantalla));
  }

  printf("\n=== 3. Seguimiento continuo: 0 -> 720 -> -720 sin saltos ===\n");
  {
    FcHorizonState st; fchReset(&st);
    uint32_t t = 0;
    float maxSalto = 0.0f, prev = 0.0f; bool first = true;
    // 2 vueltas hacia delante y 4 hacia atras, a 1 grado por muestra (100 Hz)
    std::vector<float> recorrido;
    for (float a = 0; a <= 720.0f; a += 1.0f) recorrido.push_back(a);
    for (float a = 720.0f; a >= -720.0f; a -= 1.0f) recorrido.push_back(a);
    for (float a : recorrido) {
      float h, c; horizonOf(pose(a, 0), FCH_PLANE_XY, &h, &c);
      fchUpdate(&st, h, c > 0.17f, t);
      if (!first) { float d = fabsf(st.cont - prev); if (d > maxSalto) maxSalto = d; }
      prev = st.cont; first = false;
      t += 10;
    }
    check("el continuo llega a +720 tras el recorrido (signo del horizonte)",
          fabsf(st.cont - 720.0f) < 0.5f, f("cont=%.3f", st.cont));
    check("ningun salto mayor que el paso real (1 grado)",
          maxSalto < 1.5f, f("mayor salto=%.4f grados", maxSalto));
  }

  printf("\n=== 4. Cruce de +-180 en ambos sentidos ===\n");
  {
    FcHorizonState st; fchReset(&st);
    uint32_t t = 0; float maxSalto = 0, prev = 0; bool first = true;
    for (float a = 170.0f; a <= 190.0f; a += 0.5f) {
      float h, c; horizonOf(pose(a, 0), FCH_PLANE_XY, &h, &c);
      fchUpdate(&st, h, c > 0.17f, t);
      if (!first) { float d = fabsf(st.cont - prev); if (d > maxSalto) maxSalto = d; }
      prev = st.cont; first = false; t += 10;
    }
    check("179 -> 180 -> 181 sin discontinuidad",
          maxSalto < 0.9f && fabsf(st.cont - (-190.0f)) < 0.5f,
          f("cont final=%.3f  mayor salto=%.4f", st.cont, maxSalto));
  }

  printf("\n=== 5. Sentido: la correccion es contraria al giro fisico ===\n");
  {
    for (float giro : {5.0f, -5.0f, 30.0f, -30.0f, 120.0f}) {
      FcHorizonState st; fchReset(&st);
      uint32_t t = 0;
      float ref = 0.0f;
      // Estabiliza en 0 y toma referencia
      for (int i = 0; i < 60; i++) {
        float h, c; horizonOf(pose(0, 0), FCH_PLANE_XY, &h, &c);
        fchUpdate(&st, h, c > 0.17f, t); t += 10;
      }
      ref = st.filt;
      // Gira y deja asentar
      for (int i = 0; i < 400; i++) {
        float a = giro * fminf(1.0f, i / 60.0f);
        float h, c; horizonOf(pose(a, 0), FCH_PLANE_XY, &h, &c);
        fchUpdate(&st, h, c > 0.17f, t); t += 10;
      }
      float comp = -(st.filt - ref);
      // La compensacion debe deshacer exactamente la inclinacion que el giro
      // provoca en el contenido: contenido inclinado -giro, compensacion +giro.
      bool magOk   = fabsf(comp - giro) < 0.6f;
      float residual = fchWrap180(-giro + comp);
      check((std::string("giro ") + std::to_string((int)giro) +
             " -> se compensa y no queda inclinacion").c_str(),
            magOk && fabsf(residual) < 0.6f,
            f("giro=%.1f  compensacion=%.3f  residual=%.3f", giro, comp, residual));
    }
  }

  printf("\n=== 6. Limite fisico: optica al cenit / al nadir ===\n");
  {
    for (float p : {0.0f, 45.0f, 80.0f, 89.0f, 90.0f}) {
      float h, c; horizonOf(pose(0, p), FCH_PLANE_XY, &h, &c);
      printf("       pitch %5.1f grados -> confianza %.4f%s\n", p, c,
             (c > 0.17f) ? "" : "   (sin referencia)");
    }
    float h, c;
    horizonOf(pose(0, 90.0f), FCH_PLANE_XY, &h, &c);
    check("apuntando al cenit la confianza cae a ~0", c < 0.02f, f("conf=%.5f", c));
    horizonOf(pose(0, -90.0f), FCH_PLANE_XY, &h, &c);
    check("apuntando al nadir la confianza cae a ~0", c < 0.02f, f("conf=%.5f", c));
    horizonOf(pose(0, 0.0f), FCH_PLANE_XY, &h, &c);
    check("al horizonte la confianza es 1", c > 0.999f, f("conf=%.5f", c));
  }

  printf("\n=== 7. Sin referencia: congelar y volver deslizando ===\n");
  {
    FcHorizonState st; fchReset(&st);
    uint32_t t = 0;
    for (int i = 0; i < 100; i++) {                     // asentar en 20 grados
      float h, c; horizonOf(pose(20, 0), FCH_PLANE_XY, &h, &c);
      fchUpdate(&st, h, c > 0.17f, t); t += 10;
    }
    float congelado = st.filt;
    check("asentado en -20 grados", fabsf(congelado - (-20.0f)) < 0.4f, f("filt=%.3f", congelado));

    // Apuntar al cenit 1 s: no debe moverse ni un pelo
    float maxDeriva = 0.0f;
    for (int i = 0; i < 100; i++) {
      float h, c; horizonOf(pose(160, 90), FCH_PLANE_XY, &h, &c);  // ruido de ángulo
      fchUpdate(&st, h, c > 0.17f, t); t += 10;
      maxDeriva = fmaxf(maxDeriva, fabsf(st.filt - congelado));
    }
    check("sin referencia el angulo queda congelado exacto",
          maxDeriva < 1e-6f, f("deriva=%.9f grados", maxDeriva));

    // Volver a 35 grados: debe deslizar, sin salto de golpe
    float primerPaso = 0.0f;
    for (int i = 0; i < 200; i++) {
      float h, c; horizonOf(pose(35, 0), FCH_PLANE_XY, &h, &c);
      float antes = st.filt;
      fchUpdate(&st, h, c > 0.17f, t); t += 10;
      if (i == 0) primerPaso = fabsf(st.filt - antes);
    }
    check("al recuperar referencia desliza, no salta",
          primerPaso < 2.0f, f("primer paso=%.3f grados", primerPaso));
    check("y acaba en el angulo verdadero", fabsf(st.filt - (-35.0f)) < 0.4f,
          f("filt=%.3f", st.filt));
  }

  printf("\n=== 8. Montaje: rotacion e inversion ===\n");
  {
    float h, c; horizonOf(pose(30, 0), FCH_PLANE_XY, &h, &c);
    check("montaje 0 grados", fabsf(fchApplyMount(h, 0, false) - (-30.0f)) < 0.02f,
          f("%.3f", fchApplyMount(h, 0, false)));
    check("montaje 90 grados resta 90",
          fabsf(fchApplyMount(h, 90, false) - (-120.0f)) < 0.02f,
          f("%.3f", fchApplyMount(h, 90, false)));
    check("montaje 180 grados",
          fabsf(fchAngDiff(fchApplyMount(h, 180, false), 150.0f)) < 0.02f,
          f("%.3f", fchApplyMount(h, 180, false)));
    check("inversion cambia el signo",
          fabsf(fchApplyMount(h, 0, true) - 30.0f) < 0.02f,
          f("%.3f", fchApplyMount(h, 0, true)));
  }

  printf("\n=== 9. Zona muerta y respuesta ===\n");
  {
    FcHorizonState st; fchReset(&st);
    uint32_t t = 0;
    for (int i = 0; i < 50; i++) { fchUpdate(&st, 0.0f, true, t); t += 10; }
    float base = st.filt;
    for (int i = 0; i < 50; i++) { fchUpdate(&st, 0.15f, true, t); t += 10; }  // < zona muerta
    check("un temblor de 0.15 grados no mueve la imagen",
          fabsf(st.filt - base) < 1e-6f, f("desvio=%.9f", st.filt - base));

    fchReset(&st); t = 0;
    for (int i = 0; i < 50; i++) { fchUpdate(&st, 0.0f, true, t); t += 10; }
    int pasos = 0;
    while (fabsf(st.filt - 45.0f) > 45.0f * 0.1f && pasos < 500) {  // hasta el 90%
      fchUpdate(&st, 45.0f, true, t); t += 10; pasos++;
    }
    check("un giro real de 45 grados se sigue en menos de 150 ms",
          pasos * 10 < 150, f("%.0f ms", (double)pasos * 10));
  }

  printf("\n=== 10. Planos de montaje alternativos ===\n");
  {
    // Con el modulo girado 90 grados sobre el eje X, el plano imagen pasa
    // a ser XZ. Comprobamos que la opcion de plano lo recupera.
    Q q = mul(pose(25, 0), axisAngle(1, 0, 0, 90));
    float hXY, cXY, hXZ, cXZ;
    horizonOf(q, FCH_PLANE_XY, &hXY, &cXY);
    horizonOf(q, FCH_PLANE_XZ, &hXZ, &cXZ);
    printf("       plano XY -> ang=%.2f conf=%.3f | plano XZ -> ang=%.2f conf=%.3f\n",
           hXY, cXY, hXZ, cXZ);
    check("con el modulo girado, el plano XZ recupera la confianza",
          cXZ > 0.99f && cXY < 0.5f, f("confXZ=%.3f confXY=%.3f", cXZ, cXY));
  }

  printf("\n=== %d pruebas, %d fallidas ===\n", pruebas, fallos);
  return fallos ? 1 : 0;
}

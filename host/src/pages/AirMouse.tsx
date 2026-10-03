import * as api from "../api";
import { AIR_ODR_HZ, AIR_SENS } from "../constants";
import type { Apply, Settings } from "../types";

interface Props {
  settings: Settings;
  apply: Apply;
}

export default function AirMouse({ settings, apply }: Props) {
  return (
    <div>
      <h2>Air Mouse</h2>

      <section className="card">
        <h3>Sensitivity</h3>
        <select
          value={settings.air_sens}
          onChange={(e) =>
            void apply(async () => {
              const air_sens = await api.setAirSens(Number(e.target.value));
              return {
                air_sens,
                air_sens_pair: [AIR_SENS[air_sens][0], AIR_SENS[air_sens][1]],
              };
            })
          }
        >
          {AIR_SENS.map(([x, y], index) => (
            <option key={index} value={index}>
              {index} · {x}/{y} counts/deg
            </option>
          ))}
        </select>
        <p className="muted">
          Current preset {settings.air_sens} ({settings.air_sens_pair[0]}/
          {settings.air_sens_pair[1]} counts/deg)
        </p>
      </section>

      <section className="card">
        <h3>IMU output rate</h3>
        <select
          value={settings.air_odr}
          onChange={(e) =>
            void apply(async () => {
              const air_odr = await api.setAirOdr(Number(e.target.value));
              return { air_odr, air_odr_hz: AIR_ODR_HZ[air_odr] };
            })
          }
        >
          {AIR_ODR_HZ.map((hz, index) => (
            <option key={hz} value={index}>
              {index} · {hz} Hz
            </option>
          ))}
        </select>
        <p className="muted">
          Current preset {settings.air_odr} ({settings.air_odr_hz} Hz)
        </p>
      </section>
    </div>
  );
}

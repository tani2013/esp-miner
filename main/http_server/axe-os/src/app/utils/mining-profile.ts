export type MiningProfile = 'eco' | 'balanced' | 'turbo';

export interface AsicProfileInfo {
  defaultFrequency: number;
  frequencyOptions: number[];
  defaultVoltage: number;
  voltageOptions: number[];
}

export interface ProfileSettings {
  frequency: number;
  coreVoltage: number;
  asgErrorTarget: number;
}

/**
 * Returns the index of the value in a (already ascending-sorted) options array,
 * falling back to the index of the nearest value if an exact match is missing.
 */
function nearestIndex(options: number[], value: number): number {
  let bestIndex = 0;
  let bestDistance = Infinity;
  for (let i = 0; i < options.length; i++) {
    if (options[i] === value) {
      return i;
    }
    const distance = Math.abs(options[i] - value);
    if (distance < bestDistance) {
      bestDistance = distance;
      bestIndex = i;
    }
  }
  return bestIndex;
}

/**
 * Picks the device-safe option one `step` away from the default value.
 * Clamps to the array bounds. If the options array is empty/missing it falls
 * back to the provided default value so we never return NaN/undefined.
 */
function steppedOption(options: number[] | undefined, defaultValue: number, step: number): number {
  if (!options || options.length === 0) {
    return defaultValue;
  }

  const sorted = [...options].sort((a, b) => a - b);
  const baseIndex = nearestIndex(sorted, defaultValue);
  const targetIndex = Math.min(sorted.length - 1, Math.max(0, baseIndex + step));
  return sorted[targetIndex];
}

export function computeMiningProfile(profile: MiningProfile, asic: AsicProfileInfo): ProfileSettings {
  const defaultFrequency = asic.defaultFrequency;
  const defaultVoltage = asic.defaultVoltage;

  switch (profile) {
    case 'eco':
      return {
        frequency: steppedOption(asic.frequencyOptions, defaultFrequency, -1),
        coreVoltage: steppedOption(asic.voltageOptions, defaultVoltage, -1),
        asgErrorTarget: 1
      };
    case 'turbo':
      return {
        frequency: steppedOption(asic.frequencyOptions, defaultFrequency, 1),
        coreVoltage: steppedOption(asic.voltageOptions, defaultVoltage, 1),
        asgErrorTarget: 3
      };
    case 'balanced':
    default:
      return {
        frequency: defaultFrequency,
        coreVoltage: defaultVoltage,
        asgErrorTarget: 2
      };
  }
}

/**
 * Mining efficiency in Joules per Terahash (W per TH/s).
 * Returns null when the inputs are non-positive (avoids divide-by-zero / NaN).
 */
export function efficiencyJTH(powerW: number, hashRateGHs: number): number | null {
  if (hashRateGHs <= 0 || powerW <= 0) {
    return null;
  }
  return powerW / (hashRateGHs / 1000);
}

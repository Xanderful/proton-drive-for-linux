export interface CacheEntry<T> {
    status: number;
    value: T;
}

export interface SearchParameters {
    address?: string;
    from?: string;
    to?: string;
    keyword?: string;
    begin?: number;
    end?: number;
    wildcard?: number;
}
